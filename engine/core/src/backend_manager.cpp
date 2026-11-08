#include "backend_internal.hpp"

namespace paperboat::core::detail {

#ifdef _WIN32
void net_init() {
  static const bool once = [] {
    WSADATA data;
    return WSAStartup(MAKEWORD(2, 2), &data) == 0;
  }();
  (void)once;
}
void close_sock(sock_t s) { closesocket(s); }
void set_nonblocking(sock_t s) {
  u_long on = 1;
  ioctlsocket(s, FIONBIO, &on);
}
bool connect_pending() { return WSAGetLastError() == WSAEWOULDBLOCK; }
#else
void net_init() {}
void close_sock(sock_t s) { close(s); }
void set_nonblocking(sock_t s) { fcntl(s, F_SETFL, O_NONBLOCK); }
bool connect_pending() { return errno == EINPROGRESS; }
#endif

int64_t current_timestamp() {
  return std::chrono::duration_cast<std::chrono::seconds>(
      std::chrono::system_clock::now().time_since_epoch()).count();
}

double seconds_since(std::chrono::steady_clock::time_point start) {
  return std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count();
}

std::string ws_url(int port) { return "ws://localhost:" + std::to_string(port); }

// The executable a C++ backend's build made: the first `paperboat-*` file in `build_dir`
// that isn't an object or an archive.
fs::path built_binary(const fs::path& build_dir) {
  std::error_code ec;
  for (const auto& entry : fs::directory_iterator(build_dir, ec)) {
    if (!entry.is_regular_file()) continue;
    const auto name = entry.path().filename().string();
    if (name.starts_with("paperboat-") && !name.ends_with(".o") && !name.ends_with(".a")) return entry.path();
  }
  return {};
}

// `name.pkk` is a common typo for `name.ppk`: take the .ppk when only it exists.
fs::path fix_pkk_typo(fs::path p) {
  if (!fs::exists(p) && p.extension() == ".pkk") {
    fs::path ppk = p;
    ppk.replace_extension(".ppk");
    if (fs::exists(ppk)) p = ppk;
  }
  return p;
}

// A packed backend: a .ppk (or .paperboat-pkg, or the .pkk typo) file.
bool is_package(const fs::path& p) {
  return fs::is_regular_file(p) &&
         (p.extension() == ".ppk" || p.extension() == ".paperboat-pkg" || p.extension() == ".pkk");
}

// What a failed helper process said: its stdout, else its stderr.
const std::string& output_of(const ProcessResult& r) {
  return !r.stdout_text.empty() ? r.stdout_text : r.stderr_text;
}

}  // namespace paperboat::core::detail
