#pragma once

// Internal to the files that implement BackendManager (backend_*.cpp, webapp.cpp): the shared
// includes and the helpers more than one of them uses. Not installed.

#include "paperboat/core/backend_manager.hpp"

#include <algorithm>
#include <array>
#include <chrono>
#include <csignal>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <iostream>
#include <sstream>
#include <thread>

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <winsock2.h>
#include <ws2tcpip.h>
#include <iphlpapi.h>
#include <windows.h>
#else
#include <dirent.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <unistd.h>
#endif

#include <nlohmann/json.hpp>
#include <toml++/toml.hpp>

#include "paperboat/core/process.hpp"
#include "paperboat/toolkit/descriptor.hpp"
#include "paperboat/toolkit/installer.hpp"
#include "paperboat/toolkit/platform.hpp"
#include "paperboat/toolkit/solve.hpp"

namespace paperboat::core::detail {

namespace fs = std::filesystem;
using nlohmann::json;

// Sockets, the same calls on POSIX and Winsock.
#ifdef _WIN32
using sock_t = SOCKET;
using sock_len = int;
constexpr sock_t kNoSock = INVALID_SOCKET;
#else
using sock_t = int;
using sock_len = socklen_t;
constexpr sock_t kNoSock = -1;
#endif
void net_init();
void close_sock(sock_t s);
void set_nonblocking(sock_t s);
bool connect_pending();

int64_t current_timestamp();
double seconds_since(std::chrono::steady_clock::time_point start);
std::string ws_url(int port);

// The executable a C++ backend's build made: the first `paperboat-*` file in `build_dir`
// that isn't an object or an archive.
fs::path built_binary(const fs::path& build_dir);

// `name.pkk` is a common typo for `name.ppk`: take the .ppk when only it exists.
fs::path fix_pkk_typo(fs::path p);

// A packed backend: a .ppk (or .paperboat-pkg, or the .pkk typo) file.
bool is_package(const fs::path& p);

// What a failed helper process said: its stdout, else its stderr.
const std::string& output_of(const ProcessResult& r);

}  // namespace paperboat::core::detail
