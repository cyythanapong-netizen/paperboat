// ProcessRunner on Windows (process.cpp is the POSIX one): CreateProcessW with pipes for output, a job
// object per child standing in for the process group, and taskkill for processes we did not start.
#include "paperboat/core/process.hpp"

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <winternl.h>  // UNICODE_STRING

#include <algorithm>
#include <array>
#include <cstddef>
#include <atomic>
#include <chrono>
#include <map>
#include <mutex>
#include <thread>

namespace paperboat::core {
namespace {

namespace fs = std::filesystem;
using Env = std::vector<std::pair<std::string, std::string>>;

std::wstring widen(std::string_view s) {
  if (s.empty()) return {};
  const int n = MultiByteToWideChar(CP_UTF8, 0, s.data(), static_cast<int>(s.size()), nullptr, 0);
  std::wstring w(static_cast<std::size_t>(n), L'\0');
  MultiByteToWideChar(CP_UTF8, 0, s.data(), static_cast<int>(s.size()), w.data(), n);
  return w;
}

std::string narrow(std::wstring_view w) {
  if (w.empty()) return {};
  const int n = WideCharToMultiByte(CP_UTF8, 0, w.data(), static_cast<int>(w.size()), nullptr, 0, nullptr, nullptr);
  std::string s(static_cast<std::size_t>(n), '\0');
  WideCharToMultiByte(CP_UTF8, 0, w.data(), static_cast<int>(w.size()), s.data(), n, nullptr, nullptr);
  return s;
}

// One argument as CommandLineToArgvW reads it back ("Everyone quotes command line arguments the wrong way").
void append_quoted(std::wstring& out, const std::wstring& arg) {
  if (!arg.empty() && arg.find_first_of(L" \t\n\v\"") == std::wstring::npos) {
    out += arg;
    return;
  }
  out += L'"';
  for (auto it = arg.begin();; ++it) {
    std::size_t backslashes = 0;
    while (it != arg.end() && *it == L'\\') {
      ++it;
      ++backslashes;
    }
    if (it == arg.end()) {
      out.append(backslashes * 2, L'\\');
      break;
    }
    if (*it == L'"') {
      out.append(backslashes * 2 + 1, L'\\');
      out += L'"';
    } else {
      out.append(backslashes, L'\\');
      out += *it;
    }
  }
  out += L'"';
}

bool on_path(const std::wstring& name, const wchar_t* ext) {
  wchar_t found[MAX_PATH];
  return SearchPathW(nullptr, name.c_str(), ext, MAX_PATH, found, nullptr) != 0;
}

// npm, npx and friends are .cmd files, which CreateProcess runs only through cmd.exe.
std::vector<std::string> with_cmd_for_scripts(const std::vector<std::string>& args) {
  const std::wstring name = widen(args[0]);
  if (on_path(name, L".exe")) return args;
  if (on_path(name, L".cmd") || on_path(name, L".bat")) {
    std::vector<std::string> wrapped = {"cmd.exe", "/c"};
    wrapped.insert(wrapped.end(), args.begin(), args.end());
    return wrapped;
  }
  return args;
}

std::wstring command_line(const std::vector<std::string>& args) {
  std::wstring line;
  for (std::size_t i = 0; i < args.size(); ++i) {
    if (i) line += L' ';
    append_quoted(line, widen(args[i]));
  }
  return line;
}

bool same_name(const std::wstring& a, const std::wstring& b) {
  return CompareStringOrdinal(a.c_str(), static_cast<int>(a.size()), b.c_str(), static_cast<int>(b.size()), TRUE) == CSTR_EQUAL;
}

// `custom_env` first, then every inherited variable it doesn't override, as a double-NUL block.
std::wstring environment_block(const Env& custom) {
  std::vector<std::wstring> entries;
  std::vector<std::wstring> names;
  for (const auto& [k, v] : custom) {
    names.push_back(widen(k));
    entries.push_back(widen(k) + L"=" + widen(v));
  }
  if (wchar_t* env = GetEnvironmentStringsW()) {
    for (const wchar_t* p = env; *p; p += wcslen(p) + 1) {
      std::wstring entry = p;
      const auto eq = entry.find(L'=', 1);  // entries like "=C:=C:\" begin with '='
      if (eq == std::wstring::npos) continue;
      const std::wstring name = entry.substr(0, eq);
      if (std::ranges::none_of(names, [&](const std::wstring& n) { return same_name(n, name); })) entries.push_back(entry);
    }
    FreeEnvironmentStringsW(env);
  }
  std::wstring block;
  for (const auto& e : entries) {
    block += e;
    block += L'\0';
  }
  block += L'\0';
  return block;
}

// Jobs of the children this process started, so kill_pgid(pid) can end a whole tree.
std::mutex g_jobs_mutex;
std::map<DWORD, HANDLE> g_jobs;
std::atomic<HANDLE> g_active_job{nullptr};

HANDLE make_job() {
  HANDLE job = CreateJobObjectW(nullptr, nullptr);
  if (!job) return nullptr;
  JOBOBJECT_EXTENDED_LIMIT_INFORMATION limits{};
  limits.BasicLimitInformation.LimitFlags = JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE;  // children die with us
  SetInformationJobObject(job, JobObjectExtendedLimitInformation, &limits, sizeof(limits));
  return job;
}

struct Child {
  HANDLE process = nullptr;
  HANDLE job = nullptr;
  DWORD pid = 0;
  HANDLE out = nullptr;  // read ends
  HANDLE err = nullptr;
};

void close_handle(HANDLE& h) {
  if (h && h != INVALID_HANDLE_VALUE) CloseHandle(h);
  h = nullptr;
}

HANDLE open_nul(DWORD access) {
  SECURITY_ATTRIBUTES sa{sizeof(sa), nullptr, TRUE};
  return CreateFileW(L"NUL", access, FILE_SHARE_READ | FILE_SHARE_WRITE, &sa, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
}

// Start `args` with the given standard handles (all inheritable). Returns false on failure.
bool create(const std::vector<std::string>& args, const fs::path& cwd, const Env& env, HANDLE in, HANDLE out, HANDLE err,
            DWORD flags, bool in_job, Child& child) {
  const std::wstring dir = cwd.empty() ? std::wstring{} : cwd.wstring();
  std::wstring line = command_line(with_cmd_for_scripts(args));
  const std::wstring block = environment_block(env);
  STARTUPINFOW si{};
  si.cb = sizeof(si);
  si.dwFlags = STARTF_USESTDHANDLES;
  si.hStdInput = in;
  si.hStdOutput = out;
  si.hStdError = err;
  PROCESS_INFORMATION pi{};
  if (in_job) flags |= CREATE_SUSPENDED;  // joins its job before running a line of code
  if (!CreateProcessW(nullptr, line.data(), nullptr, nullptr, TRUE, flags | CREATE_UNICODE_ENVIRONMENT, const_cast<wchar_t*>(block.c_str()),
                      dir.empty() ? nullptr : dir.c_str(), &si, &pi))
    return false;
  child.process = pi.hProcess;
  child.pid = pi.dwProcessId;
  if (in_job) {
    child.job = make_job();
    if (child.job) AssignProcessToJobObject(child.job, pi.hProcess);
    ResumeThread(pi.hThread);
    std::lock_guard lock(g_jobs_mutex);
    g_jobs[child.pid] = child.job;
  }
  CloseHandle(pi.hThread);
  return true;
}

bool spawn_piped(const std::vector<std::string>& args, const fs::path& cwd, const Env& env, Child& child) {
  SECURITY_ATTRIBUTES sa{sizeof(sa), nullptr, TRUE};
  HANDLE out_r = nullptr, out_w = nullptr, err_r = nullptr, err_w = nullptr;
  if (!CreatePipe(&out_r, &out_w, &sa, 0) || !CreatePipe(&err_r, &err_w, &sa, 0)) return false;
  SetHandleInformation(out_r, HANDLE_FLAG_INHERIT, 0);
  SetHandleInformation(err_r, HANDLE_FLAG_INHERIT, 0);
  HANDLE nul = open_nul(GENERIC_READ);
  const bool ok = create(args, cwd, env, nul, out_w, err_w, CREATE_NO_WINDOW | CREATE_NEW_PROCESS_GROUP, true, child);
  close_handle(nul);
  close_handle(out_w);
  close_handle(err_w);
  if (!ok) {
    close_handle(out_r);
    close_handle(err_r);
    return false;
  }
  child.out = out_r;
  child.err = err_r;
  return true;
}

bool finished(const Child& c, int& code) {
  if (WaitForSingleObject(c.process, 0) != WAIT_OBJECT_0) return false;
  DWORD exit = 0;
  GetExitCodeProcess(c.process, &exit);
  code = static_cast<int>(exit);
  return true;
}

void release(Child& c) {
  {
    std::lock_guard lock(g_jobs_mutex);
    g_jobs.erase(c.pid);
  }
  close_handle(c.out);
  close_handle(c.err);
  close_handle(c.process);
  close_handle(c.job);
}

void end_tree(const Child& c, UINT code) {
  if (c.job) TerminateJobObject(c.job, code);
  else TerminateProcess(c.process, code);
}

// Read both pipes until both close. Each round: `before()` (false stops), whatever is waiting on each
// pipe to `on_data` (stream 0 stdout, 1 stderr), then `after()` (false stops).
void pump(Child& c, const std::function<bool()>& before, const std::function<void(int, std::string_view)>& on_data,
          const std::function<bool()>& after) {
  std::array<HANDLE*, 2> pipes{&c.out, &c.err};
  std::array<char, 4096> buf{};
  while (*pipes[0] || *pipes[1]) {
    if (!before()) break;
    bool got = false;
    for (int i = 0; i < 2; ++i) {
      HANDLE& h = *pipes[static_cast<std::size_t>(i)];
      if (!h) continue;
      DWORD avail = 0;
      if (!PeekNamedPipe(h, nullptr, 0, nullptr, &avail, nullptr)) {  // the writer is gone and the pipe is drained
        close_handle(h);
        continue;
      }
      DWORD n = 0;
      if (avail > 0 && ReadFile(h, buf.data(), static_cast<DWORD>(std::min<std::size_t>(avail, buf.size())), &n, nullptr) && n > 0) {
        on_data(i, {buf.data(), n});
        got = true;
      }
    }
    if (!after()) break;
    if (!got) std::this_thread::sleep_for(std::chrono::milliseconds(10));
  }
}

void process_buffer_lines(std::string& buffer, std::string_view stream_name, const ProcessOutputCallback& cb) {
  std::size_t pos = 0;
  while ((pos = buffer.find('\n')) != std::string::npos) {
    std::string line = buffer.substr(0, pos);
    while (!line.empty() && line.back() == '\r') line.pop_back();
    if (cb) cb(stream_name, line);
    buffer.erase(0, pos + 1);
  }
}

std::string trim_trailing_newlines(std::string_view s) {
  while (!s.empty() && (s.back() == '\n' || s.back() == '\r')) s.remove_suffix(1);
  return std::string(s);
}

BOOL WINAPI on_ctrl(DWORD type) {  // Ctrl-C / Ctrl-Break: end the child we are streaming, then carry on
  if (type != CTRL_C_EVENT && type != CTRL_BREAK_EVENT) return FALSE;
  if (HANDLE job = g_active_job.load()) {
    TerminateJobObject(job, 130);
    return TRUE;
  }
  return FALSE;
}

HANDLE open_process(int pid, DWORD access) { return OpenProcess(access, FALSE, static_cast<DWORD>(pid)); }

bool wait_gone(int pid, int timeout_ms) {
  HANDLE h = open_process(pid, SYNCHRONIZE);
  if (!h) return true;  // no such process
  const bool gone = WaitForSingleObject(h, static_cast<DWORD>(timeout_ms)) == WAIT_OBJECT_0;
  CloseHandle(h);
  return gone;
}

}  // namespace

ProcessResult ProcessRunner::run_sync(const std::vector<std::string>& args, const fs::path& cwd, const Env& env,
                                      std::chrono::milliseconds timeout) {
  ProcessResult result;
  if (args.empty()) return result;
  Child child;
  if (!spawn_piped(args, cwd, env, child)) {
    result.exit_code = -1;
    return result;
  }
  const auto start = std::chrono::steady_clock::now();
  pump(
      child,
      [&] {
        if (timeout.count() > 0 && std::chrono::steady_clock::now() - start >= timeout) {
          result.timed_out = true;
          end_tree(child, 137);
          return false;
        }
        return true;
      },
      [&](int stream, std::string_view chunk) { (stream == 0 ? result.stdout_text : result.stderr_text).append(chunk); },
      [&] {
        finished(child, result.exit_code);
        return true;
      });
  if (result.timed_out) {
    WaitForSingleObject(child.process, 2000);
    result.exit_code = 137;
  } else {
    WaitForSingleObject(child.process, INFINITE);
    finished(child, result.exit_code);
  }
  release(child);
  return result;
}

int ProcessRunner::run_stream(const std::vector<std::string>& args, const fs::path& cwd, const Env& env,
                              ProcessOutputCallback on_output, std::stop_token stop) {
  if (args.empty()) return -1;
  Child child;
  if (!spawn_piped(args, cwd, env, child)) return -1;

  g_active_job.store(child.job);
  SetConsoleCtrlHandler(on_ctrl, TRUE);

  int exit_code = -1;
  bool reaped = false;
  std::array<std::string, 2> line_bufs;  // stdout, stderr: the part after the last newline
  constexpr std::array<std::string_view, 2> names{"stdout", "stderr"};
  pump(
      child,
      [&] {
        if (stop.stop_requested()) {
          end_tree(child, 143);
          return false;
        }
        return true;
      },
      [&](int stream, std::string_view chunk) {
        const auto at = static_cast<std::size_t>(stream);
        line_bufs[at].append(chunk);
        process_buffer_lines(line_bufs[at], names[at], on_output);
      },
      [&] {
        reaped = finished(child, exit_code);
        return !reaped;
      });

  for (std::size_t i = 0; i < 2; ++i)
    if (!line_bufs[i].empty() && on_output) on_output(names[i], trim_trailing_newlines(line_bufs[i]));

  if (!reaped) {  // the pipes closed first, or we were stopped: end what is left, then take its status
    end_tree(child, 143);
    WaitForSingleObject(child.process, 2000);
    reaped = finished(child, exit_code);
  } else if (child.job) {
    TerminateJobObject(child.job, 143);  // stragglers; the main process already has its status
  }

  g_active_job.store(nullptr);
  SetConsoleCtrlHandler(on_ctrl, FALSE);
  release(child);
  return reaped ? exit_code : -1;
}

int ProcessRunner::run_detached(const std::vector<std::string>& args, const fs::path& cwd, const Env& env,
                                const fs::path& log_file) {
  if (args.empty()) return -1;
  SECURITY_ATTRIBUTES sa{sizeof(sa), nullptr, TRUE};
  HANDLE nul = open_nul(GENERIC_READ);
  HANDLE out = nullptr;
  if (!log_file.empty()) {
    std::error_code ec;
    fs::create_directories(log_file.parent_path(), ec);
    out = CreateFileW(log_file.wstring().c_str(), FILE_APPEND_DATA, FILE_SHARE_READ | FILE_SHARE_WRITE, &sa, OPEN_ALWAYS,
                      FILE_ATTRIBUTE_NORMAL, nullptr);
    if (out == INVALID_HANDLE_VALUE) out = nullptr;
  }
  if (!out) out = open_nul(GENERIC_WRITE);

  Child child;
  const bool ok = create(args, cwd, env, nul, out, out, DETACHED_PROCESS | CREATE_NEW_PROCESS_GROUP, false, child);
  close_handle(nul);
  close_handle(out);
  if (!ok) return -1;
  const int pid = static_cast<int>(child.pid);
  close_handle(child.process);
  return pid;
}

bool ProcessRunner::is_alive(int pid) {
  if (pid <= 0) return false;
  HANDLE h = open_process(pid, SYNCHRONIZE);
  if (!h) return false;
  const bool alive = WaitForSingleObject(h, 0) == WAIT_TIMEOUT;
  CloseHandle(h);
  return alive;
}

bool ProcessRunner::kill_pid(int pid, int sig, int timeout_ms) {
  if (!is_alive(pid)) return true;
  if (HANDLE h = open_process(pid, PROCESS_TERMINATE)) {  // Windows has no SIGTERM for a console process
    TerminateProcess(h, static_cast<UINT>(128 + sig));
    CloseHandle(h);
  }
  return wait_gone(pid, timeout_ms);
}

bool ProcessRunner::kill_pgid(int pgid, int sig, int timeout_ms) {
  if (pgid <= 1) return true;
  HANDLE job = nullptr;
  {
    std::lock_guard lock(g_jobs_mutex);
    if (auto it = g_jobs.find(static_cast<DWORD>(pgid)); it != g_jobs.end()) job = it->second;
  }
  if (job) TerminateJobObject(job, static_cast<UINT>(128 + sig));
  else run_sync({"taskkill", "/PID", std::to_string(pgid), "/T", "/F"});  // a process we did not start: its tree
  return wait_gone(pgid, timeout_ms);
}

// The command line the process was started with (NtQueryInformationProcess, ProcessCommandLineInformation,
// Windows 8.1 and later), else its program's path. Empty when the process cannot be opened.
std::string ProcessRunner::read_cmdline(int pid) {
  if (pid <= 0) return {};
  HANDLE h = open_process(pid, PROCESS_QUERY_LIMITED_INFORMATION);
  if (!h) return {};
  std::wstring text;
  using QueryInformation = LONG(WINAPI*)(HANDLE, ULONG, PVOID, ULONG, PULONG);
  static const auto query = reinterpret_cast<QueryInformation>(
      reinterpret_cast<void*>(GetProcAddress(GetModuleHandleW(L"ntdll.dll"), "NtQueryInformationProcess")));
  constexpr ULONG kProcessCommandLineInformation = 60;
  if (query) {
    ULONG size = 0;
    query(h, kProcessCommandLineInformation, nullptr, 0, &size);  // fails, and says how big a buffer it needs
    if (size >= sizeof(UNICODE_STRING)) {
      std::vector<std::byte> buf(size);
      if (query(h, kProcessCommandLineInformation, buf.data(), size, &size) >= 0) {
        const auto* line = reinterpret_cast<const UNICODE_STRING*>(buf.data());
        if (line->Buffer) text.assign(line->Buffer, line->Length / sizeof(wchar_t));
      }
    }
  }
  if (text.empty()) {
    std::wstring image(MAX_PATH, L'\0');
    DWORD n = static_cast<DWORD>(image.size());
    if (QueryFullProcessImageNameW(h, 0, image.data(), &n)) text.assign(image.data(), n);
  }
  CloseHandle(h);
  return narrow(text);
}

}  // namespace paperboat::core
