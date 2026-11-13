#include "paperboat/toolkit/platform.hpp"

#include <cstdlib>
#include <fstream>
#include <string_view>

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <io.h>
#include <process.h>
#include <windows.h>
#else
#include <unistd.h>
#endif
#ifdef __APPLE__
#include <mach-o/dyld.h>  // _NSGetExecutablePath
#endif

namespace paperboat::platform {

namespace fs = std::filesystem;

int current_pid() {
#ifdef _WIN32
  return _getpid();
#else
  return getpid();
#endif
}

bool is_tty(int fd) {
#ifdef _WIN32
  return _isatty(fd) != 0;
#else
  return isatty(fd) != 0;
#endif
}

fs::path exe_path() {
#ifdef _WIN32
  std::wstring buf(MAX_PATH, L'\0');
  for (;;) {
    const DWORD n = GetModuleFileNameW(nullptr, buf.data(), static_cast<DWORD>(buf.size()));
    if (n == 0) return {};
    if (n < buf.size()) {
      buf.resize(n);
      return fs::path(buf);
    }
    buf.resize(buf.size() * 2);
  }
#elif defined(__APPLE__)
  // no /proc on macOS: ask dyld, then resolve symlinks (an npm or Homebrew bin/ link) the way
  // /proc/self/exe does on Linux
  uint32_t size = 0;
  _NSGetExecutablePath(nullptr, &size);
  std::string buf(size, '\0');
  if (_NSGetExecutablePath(buf.data(), &size) != 0) return {};
  buf.resize(std::char_traits<char>::length(buf.c_str()));
  std::error_code ec;
  const fs::path p = fs::canonical(buf, ec);
  return ec ? fs::path(buf) : p;
#else
  std::error_code ec;
  const fs::path p = fs::read_symlink("/proc/self/exe", ec);
  return ec ? fs::path{} : p;
#endif
}

fs::path home_dir() {
#ifdef _WIN32
  const char* v = std::getenv("USERPROFILE");
#else
  const char* v = std::getenv("HOME");
#endif
  return v && *v ? fs::path(v) : fs::path{};
}

fs::path shared_tmp_dir() {
#ifdef _WIN32
  std::error_code ec;
  return fs::temp_directory_path(ec);
#else
  return "/tmp";
#endif
}

std::string cmake_command(const fs::path& build_dir) {
  const auto from_cache = [](const fs::path& dir, std::string& out) {
    std::ifstream in(dir / "CMakeCache.txt");
    if (!in) return false;
    static constexpr std::string_view key = "CMAKE_COMMAND:INTERNAL=";
    for (std::string line; std::getline(in, line);) {
      if (!line.starts_with(key)) continue;
      std::string path = line.substr(key.size());
      while (!path.empty() && (path.back() == '\r' || path.back() == ' ' || path.back() == '\t')) path.pop_back();
      std::error_code ec;
      if (!path.empty() && fs::is_regular_file(path, ec)) out = std::move(path);
      return !out.empty();
    }
    return false;
  };
  std::string found;
  for (fs::path dir = build_dir; !dir.empty(); dir = dir.parent_path()) {
    if (from_cache(dir, found)) return found;
    if (dir == dir.parent_path()) break;
  }
  for (fs::path dir = exe_path().parent_path(); !dir.empty(); dir = dir.parent_path()) {
    if (from_cache(dir, found)) return found;
    if (dir == dir.parent_path()) break;
  }
  return "cmake";
}

}  // namespace paperboat::platform
