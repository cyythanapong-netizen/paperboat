#pragma once

// The few operating-system facts the engine and the CLI ask for, the same on Linux and Windows.

#include <filesystem>
#include <string>

namespace paperboat::platform {

int current_pid();
bool is_tty(int fd);                   // 0 stdin, 1 stdout, 2 stderr
std::filesystem::path exe_path();      // the running executable, or empty
std::filesystem::path home_dir();      // $HOME or %USERPROFILE%, or empty
std::filesystem::path shared_tmp_dir();  // where separate paperboat processes meet: /tmp, or the system temp dir

// The cmake to run: the one recorded in `build_dir`'s CMakeCache.txt (or the nearest one above
// it), else the one that configured the build this program came from, else "cmake" from PATH.
// A build made by Visual Studio or a Developer prompt keeps its cmake off PATH afterwards, and
// the cache names the exact one that can read the build.
std::string cmake_command(const std::filesystem::path& build_dir);

}  // namespace paperboat::platform
