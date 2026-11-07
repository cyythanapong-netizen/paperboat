#include "paperboat/modelica/omc.hpp"

#include <dlfcn.h>
#ifdef __linux__
#include <sys/prctl.h>
#endif
#include <fcntl.h>
#include <signal.h>
#include <sys/wait.h>
#include <unistd.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <cctype>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <mutex>
#include <optional>
#include <type_traits>
#include <thread>

namespace paperboat::modelica {
namespace {

namespace fs = std::filesystem;
using namespace std::chrono_literals;

// The few libzmq calls the bridge needs, resolved from libzmq.so.5 at run time. The ABI is
// stable across libzmq 4.x: zmq_msg_t is 64 opaque bytes, the constants are fixed.
struct ZmqMsg {
  alignas(8) unsigned char bytes[64];
};
constexpr int kReq = 3, kLinger = 17, kRcvTimeo = 27, kSndTimeo = 28;

struct Zmq {
  void* (*ctx_new)() = nullptr;
  int (*ctx_term)(void*) = nullptr;
  void* (*socket)(void*, int) = nullptr;
  int (*close)(void*) = nullptr;
  int (*connect)(void*, const char*) = nullptr;
  int (*setsockopt)(void*, int, const void*, std::size_t) = nullptr;
  int (*send)(void*, const void*, std::size_t, int) = nullptr;
  int (*msg_init)(ZmqMsg*) = nullptr;
  int (*msg_recv)(ZmqMsg*, void*, int) = nullptr;
  void* (*msg_data)(ZmqMsg*) = nullptr;
  std::size_t (*msg_size)(const ZmqMsg*) = nullptr;
  int (*msg_close)(ZmqMsg*) = nullptr;

  static const Zmq& get() {
    static const Zmq zmq = [] {
      Zmq z;
      void* lib = nullptr;
      for (const char* name : {"libzmq.so.5", "libzmq.so", "libzmq.5.dylib", "libzmq.dylib"})
        if ((lib = dlopen(name, RTLD_NOW | RTLD_LOCAL))) break;
      if (!lib) return z;
      auto sym = [&](auto& fn, const char* n) { fn = reinterpret_cast<std::remove_reference_t<decltype(fn)>>(dlsym(lib, n)); };
      sym(z.ctx_new, "zmq_ctx_new");
      sym(z.ctx_term, "zmq_ctx_term");
      sym(z.socket, "zmq_socket");
      sym(z.close, "zmq_close");
      sym(z.connect, "zmq_connect");
      sym(z.setsockopt, "zmq_setsockopt");
      sym(z.send, "zmq_send");
      sym(z.msg_init, "zmq_msg_init");
      sym(z.msg_recv, "zmq_msg_recv");
      sym(z.msg_data, "zmq_msg_data");
      sym(z.msg_size, "zmq_msg_size");
      sym(z.msg_close, "zmq_msg_close");
      return z;
    }();
    if (!zmq.msg_close) throw OmcError("libzmq.so.5 not found (it comes with OpenModelica)");
    return zmq;
  }
};

std::atomic<int> sessions{0};

std::string trim(std::string s) {
  while (!s.empty() && std::isspace(static_cast<unsigned char>(s.back()))) s.pop_back();
  std::size_t i = 0;
  while (i < s.size() && std::isspace(static_cast<unsigned char>(s[i]))) ++i;
  return s.substr(i);
}

}  // namespace

std::string_view name(OmcState state) {
  switch (state) {
    case OmcState::Ready: return "ready";
    case OmcState::Checking: return "checking";
    case OmcState::Failed: return "failed";
  }
  return "failed";
}

struct Omc::Impl {
  Options options;
  std::mutex mutex;  // the queue: one request in flight
  pid_t pid = -1;
  void* ctx = nullptr;
  void* sock = nullptr;
  std::string session;
  fs::path workdir;
  std::string version;
  OmcState state = OmcState::Ready;
  int restarts = 0;  // in a row
  bool started_once = false;
  std::vector<std::string> replay;  // loadModel / loadFile calls that succeeded
  StatusFn status_fn;
  mutable std::mutex state_mutex;

  void set_state(OmcState s) {
    {
      std::lock_guard lock(state_mutex);
      if (state == s) return;
      state = s;
    }
    if (status_fn) status_fn(s, version);
  }

  fs::path port_file() const {
    // omc writes <tmp>/openmodelica.<user>.port.<session>; the user part varies, so look.
    const std::string suffix = ".port." + session;
    for (const fs::path& dir : {fs::temp_directory_path(), fs::path("/tmp")}) {
      std::error_code ec;
      for (const auto& e : fs::directory_iterator(dir, ec)) {
        const std::string n = e.path().filename().string();
        if (n.starts_with("openmodelica.") && n.ends_with(suffix)) return e.path();
      }
    }
    return {};
  }

  void stop() {
    const Zmq* z = nullptr;
    try {
      z = &Zmq::get();
    } catch (const OmcError&) {
    }
    if (z && sock) z->close(sock);
    if (z && ctx) z->ctx_term(ctx);
    sock = ctx = nullptr;
    if (pid > 0) {
      kill(pid, SIGTERM);
      for (int i = 0; i < 20 && waitpid(pid, nullptr, WNOHANG) == 0; ++i) std::this_thread::sleep_for(25ms);
      if (waitpid(pid, nullptr, WNOHANG) == 0) {
        kill(pid, SIGKILL);
        waitpid(pid, nullptr, 0);
      }
      pid = -1;
    }
    if (const fs::path f = port_file(); !f.empty()) fs::remove(f);
  }

  void start() {
    const Zmq& z = Zmq::get();
    session = "paperboat-" + std::to_string(getpid()) + "-" + std::to_string(sessions++);
    workdir = fs::temp_directory_path() / ("paperboat-omc-" + session);
    fs::create_directories(workdir);
    const std::string zarg = "-z=" + session;
    pid = fork();
    if (pid < 0) throw OmcError("cannot start omc: fork failed");
    if (pid == 0) {
#ifdef __linux__
      prctl(PR_SET_PDEATHSIG, SIGTERM);  // omc goes when we go, however we go
#endif
      if (chdir(workdir.c_str()) != 0) _exit(127);
      const int devnull = open("/dev/null", O_RDWR);
      if (devnull >= 0) {
        dup2(devnull, STDIN_FILENO);
        dup2(devnull, STDOUT_FILENO);
      }
      execlp(options.omc.c_str(), options.omc.c_str(), "--interactive=zmq", zarg.c_str(), nullptr);
      _exit(127);
    }
    fs::path file;
    for (int i = 0; i < 400 && (file = port_file()).empty(); ++i) {  // up to 20 s
      if (waitpid(pid, nullptr, WNOHANG) == pid) {
        pid = -1;
        throw OmcError("omc exited at start (is `" + options.omc + "` OpenModelica's omc?)");
      }
      std::this_thread::sleep_for(50ms);
    }
    std::string endpoint;
    for (int i = 0; i < 40 && endpoint.empty(); ++i) {  // the file can appear before it's written
      std::ifstream in(file);
      std::getline(in, endpoint);
      endpoint = trim(endpoint);
      if (endpoint.empty()) std::this_thread::sleep_for(25ms);
    }
    if (endpoint.empty()) {
      stop();
      throw OmcError("omc did not report its port");
    }
    ctx = z.ctx_new();
    sock = z.socket(ctx, kReq);
    const int linger = 0;
    z.setsockopt(sock, kLinger, &linger, sizeof linger);
    if (z.connect(sock, endpoint.c_str()) != 0) {
      stop();
      throw OmcError("cannot connect to omc at " + endpoint);
    }
    started_once = true;
  }

  // One round trip; nullopt on timeout or a dead omc.
  std::optional<std::string> exchange(std::string_view expr, std::chrono::milliseconds timeout) {
    const Zmq& z = Zmq::get();
    const int ms = static_cast<int>(timeout.count());
    z.setsockopt(sock, kSndTimeo, &ms, sizeof ms);
    z.setsockopt(sock, kRcvTimeo, &ms, sizeof ms);
    if (z.send(sock, expr.data(), expr.size(), 0) < 0) return std::nullopt;
    ZmqMsg msg;
    z.msg_init(&msg);
    if (z.msg_recv(&msg, sock, 0) < 0) {
      z.msg_close(&msg);
      return std::nullopt;
    }
    std::string out(static_cast<const char*>(z.msg_data(&msg)), z.msg_size(&msg));
    z.msg_close(&msg);
    while (!out.empty() && (out.back() == '\n' || out.back() == '\r')) out.pop_back();
    return out;
  }

  std::string call(std::string_view expr, std::chrono::milliseconds timeout) {
    std::lock_guard lock(mutex);
    if (state == OmcState::Failed) throw OmcError("omc failed: restarted " + std::to_string(restarts) + " times");
    set_state(OmcState::Checking);
    for (;;) {
      try {
        if (!sock) {
          const bool again = started_once;
          start();
          if (auto v = exchange("getVersion()", timeout)) version = trim(*v), version = version.size() >= 2 ? version.substr(1, version.size() - 2) : version;
          if (again)  // a restart: load what the old omc had loaded
            for (const auto& r : replay) exchange(r, timeout);
        }
        if (auto out = exchange(expr, timeout)) {
          restarts = 0;
          if ((expr.starts_with("loadModel(") || expr.starts_with("loadFile(")) && *out == "true")
            if (std::find(replay.begin(), replay.end(), expr) == replay.end()) replay.emplace_back(expr);
          set_state(OmcState::Ready);
          return *out;
        }
      } catch (const OmcError&) {
        stop();
        set_state(OmcState::Failed);
        throw;
      }
      stop();  // timed out or died: restart and retry, up to max_restarts in a row
      if (++restarts > options.max_restarts) {
        set_state(OmcState::Failed);
        throw OmcError("omc did not answer `" + std::string(expr.substr(0, 60)) + "` within " +
                       std::to_string(timeout.count() / 1000) + " s; gave up after " +
                       std::to_string(options.max_restarts) + " restarts");
      }
    }
  }
};

Omc::Omc() : Omc(Options{}) {}
Omc::Omc(Options options) : impl_(std::make_unique<Impl>()) { impl_->options = std::move(options); }

Omc::~Omc() {
  if (impl_->sock) {
    try {
      impl_->exchange("quit()", 1s);
    } catch (...) {
    }
  }
  impl_->stop();
  std::error_code ec;
  if (!impl_->workdir.empty()) fs::remove_all(impl_->workdir, ec);
}

std::string Omc::call(std::string_view expr) { return impl_->call(expr, impl_->options.timeout); }
std::string Omc::call(std::string_view expr, std::chrono::milliseconds timeout) { return impl_->call(expr, timeout); }

std::string Omc::errors() {
  const OmValue v = parse_value(call("getErrorString()"));
  return trim(v.atom);
}

OmcState Omc::state() const {
  std::lock_guard lock(impl_->state_mutex);
  return impl_->state;
}
std::string Omc::version() const { return impl_->version; }
void Omc::on_status(StatusFn fn) { impl_->status_fn = std::move(fn); }
std::string Omc::workdir() const { return impl_->workdir.string(); }

// --- allow-list ---

const std::vector<std::string>& allowed_calls() {
  static const std::vector<std::string> calls = {
      "loadModel", "loadFile", "getClassNames", "getComponents", "getIconAnnotation", "getDiagramAnnotation",
      "getConnectionCount", "getNthConnection", "checkModel", "addComponent", "deleteComponent", "addConnection",
      "deleteConnection", "setComponentModifierValue", "list", "dumpXMLDAE", "buildModelFMU", "getVersion",
      "getErrorString"};  // getErrorString: read-only, how a client words a failed call
  return calls;
}

bool allowed(std::string_view expr) {
  const auto& calls = allowed_calls();
  auto listed = [&](std::string_view n) { return std::find(calls.begin(), calls.end(), n) != calls.end(); };
  bool first = true;
  for (std::size_t i = 0; i < expr.size();) {
    const char c = expr[i];
    if (c == '"') {  // a string: skip it, escapes included
      for (++i; i < expr.size() && expr[i] != '"'; ++i)
        if (expr[i] == '\\') ++i;
      if (i >= expr.size()) return false;
      ++i;
      continue;
    }
    if (c == ';' || (c == ':' && i + 1 < expr.size() && expr[i + 1] == '=')) return false;
    if (std::isalpha(static_cast<unsigned char>(c)) || c == '_') {
      const std::size_t start = i;
      while (i < expr.size() && (std::isalnum(static_cast<unsigned char>(expr[i])) || expr[i] == '_' || expr[i] == '.')) ++i;
      const std::string_view ident = expr.substr(start, i - start);
      std::size_t j = i;
      while (j < expr.size() && std::isspace(static_cast<unsigned char>(expr[j]))) ++j;
      const bool is_call = j < expr.size() && expr[j] == '(';
      if (first) {  // the expression is one listed call, unqualified
        if (!is_call || !listed(ident) || start != expr.find_first_not_of(" \t\r\n")) return false;
        first = false;
      } else if (is_call) {
        const std::string_view last = ident.substr(ident.rfind('.') == std::string_view::npos ? 0 : ident.rfind('.') + 1);
        const bool record = !last.empty() && std::isupper(static_cast<unsigned char>(last[0]));
        if (!listed(last) && !record && last != "transformation" && last != "iconTransformation") return false;
        if (!record && ident.find('.') != std::string_view::npos) return false;  // OpenModelica.Scripting.x(…)
      }
      continue;
    }
    ++i;
  }
  return !first;
}

// --- values ---

namespace {

struct Parser {
  std::string_view s;
  std::size_t i = 0;

  void ws() {
    while (i < s.size() && std::isspace(static_cast<unsigned char>(s[i]))) ++i;
  }
  OmValue value() {
    ws();
    OmValue v;
    if (i >= s.size()) throw OmcError("omc answer ended early");
    if (s[i] == '{' || s[i] == '(') {  // lists and tuples alike
      const char close = s[i] == '{' ? '}' : ')';
      v.is_list = true;
      ++i;
      ws();
      if (i < s.size() && s[i] == close) {
        ++i;
        return v;
      }
      for (;;) {
        v.items.push_back(value());
        ws();
        if (i < s.size() && s[i] == ',') {
          ++i;
          continue;
        }
        if (i < s.size() && s[i] == close) {
          ++i;
          return v;
        }
        throw OmcError("malformed omc answer near: " + std::string(s.substr(i, 40)));
      }
    }
    if (s[i] == '"') {
      v.is_string = true;
      for (++i; i < s.size() && s[i] != '"'; ++i) {
        if (s[i] == '\\' && i + 1 < s.size()) {
          const char e = s[++i];
          v.atom += e == 'n' ? '\n' : e == 't' ? '\t' : e;
        } else {
          v.atom += s[i];
        }
      }
      if (i >= s.size()) throw OmcError("unterminated string in omc answer");
      ++i;
      return v;
    }
    // An atom runs to the next separator at this depth; it may hold parentheses itself
    // (a record value such as Placement(…)), kept as written.
    int depth = 0;
    const std::size_t start = i;
    for (; i < s.size(); ++i) {
      const char c = s[i];
      if (c == '"') {
        for (++i; i < s.size() && s[i] != '"'; ++i)
          if (s[i] == '\\') ++i;
      } else if (c == '(' || c == '{') {
        ++depth;
      } else if (c == ')' || c == '}') {
        if (depth == 0) break;
        --depth;
      } else if (c == ',' && depth == 0) {
        break;
      }
    }
    v.atom = trim(std::string(s.substr(start, i - start)));
    return v;
  }
};

}  // namespace

OmValue parse_value(std::string_view text) {
  Parser p{text};
  OmValue v = p.value();
  p.ws();
  if (p.i != text.size()) throw OmcError("trailing text in omc answer: " + std::string(text.substr(p.i, 40)));
  return v;
}

std::string quote(std::string_view text) {
  std::string out = "\"";
  for (const char c : text) {
    if (c == '"' || c == '\\') out += '\\';
    out += c;
  }
  return out + '"';
}

}  // namespace paperboat::modelica
