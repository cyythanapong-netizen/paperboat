#include "paperboat/toolkit/platform.hpp"

#include <cstdlib>

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <io.h>
#include <process.h>
#include <windows.h>
#else
#include <unistd.h>
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

}  // namespace paperboat::platform
