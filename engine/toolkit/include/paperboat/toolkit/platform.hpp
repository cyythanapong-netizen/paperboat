#pragma once

// The few operating-system facts the engine and the CLI ask for, the same on Linux and Windows.

#include <filesystem>

namespace paperboat::platform {

int current_pid();
bool is_tty(int fd);                   // 0 stdin, 1 stdout, 2 stderr
std::filesystem::path exe_path();      // the running executable, or empty
std::filesystem::path home_dir();      // $HOME or %USERPROFILE%, or empty
std::filesystem::path shared_tmp_dir();  // where separate paperboat processes meet: /tmp, or the system temp dir

}  // namespace paperboat::platform
