#include "backend_internal.hpp"

namespace paperboat::core {
using namespace detail;

bool BackendManager::is_port_listening(int port) {
  if (port <= 0 || port > 65535) return false;
  net_init();
  sock_t sock = socket(AF_INET, SOCK_STREAM, 0);
  if (sock == kNoSock) return false;

  // Set non-blocking for quick check
  set_nonblocking(sock);

  sockaddr_in addr{};
  addr.sin_family = AF_INET;
  addr.sin_port = htons(static_cast<uint16_t>(port));
  addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);

  int res = connect(sock, reinterpret_cast<sockaddr*>(&addr), sizeof(addr));
  if (res == 0) {
    close_sock(sock);
    return true;
  }

  if (connect_pending()) {
    fd_set fds;
    FD_ZERO(&fds);
    FD_SET(sock, &fds);
    timeval tv{.tv_sec = 0, .tv_usec = 30000};  // 30 ms
    if (select(static_cast<int>(sock) + 1, nullptr, &fds, nullptr, &tv) > 0) {
      int err = 0;
      sock_len len = sizeof(err);
      getsockopt(sock, SOL_SOCKET, SO_ERROR, reinterpret_cast<char*>(&err), &len);
      close_sock(sock);
      return err == 0;
    }
  }

  close_sock(sock);
  return false;
}

int BackendManager::find_pid_by_port(int port) {
  if (port <= 0) return 0;

#ifdef _WIN32
  ULONG size = 0;
  GetExtendedTcpTable(nullptr, &size, FALSE, AF_INET, TCP_TABLE_OWNER_PID_LISTENER, 0);
  std::vector<char> buf(size);
  if (GetExtendedTcpTable(buf.data(), &size, FALSE, AF_INET, TCP_TABLE_OWNER_PID_LISTENER, 0) != NO_ERROR) return 0;
  const auto* table = reinterpret_cast<const MIB_TCPTABLE_OWNER_PID*>(buf.data());
  for (DWORD i = 0; i < table->dwNumEntries; ++i)
    if (ntohs(static_cast<u_short>(table->table[i].dwLocalPort)) == port) return static_cast<int>(table->table[i].dwOwningPid);
  return 0;
#else

  // Search socket inode in /proc/net/tcp and /proc/net/tcp6
  std::string hex_port;
  {
    std::stringstream ss;
    ss << std::uppercase << std::hex << port;
    hex_port = ss.str();
    while (hex_port.size() < 4) hex_port = "0" + hex_port;
  }

  std::vector<std::string> socket_inodes;
  for (const char* netfile : {"/proc/net/tcp", "/proc/net/tcp6"}) {
    std::ifstream in(netfile);
    if (!in) continue;
    std::string line;
    // skip header
    std::getline(in, line);
    while (std::getline(in, line)) {
      std::istringstream iss(line);
      std::string sl, local_addr, rem_addr, st, tx_rx, tr, retr, uid, timeout, inode;
      if (iss >> sl >> local_addr >> rem_addr >> st >> tx_rx >> tr >> retr >> uid >> timeout >> inode) {
        if (st == "0A" && local_addr.ends_with(":" + hex_port)) {
          socket_inodes.push_back(inode);
        }
      }
    }
  }

  if (!socket_inodes.empty()) {
    DIR* proc = opendir("/proc");
    if (proc) {
      dirent* entry;
      while ((entry = readdir(proc)) != nullptr) {
        if (entry->d_type == DT_DIR && std::isdigit(entry->d_name[0])) {
          int pid = std::atoi(entry->d_name);
          std::string fd_dir = std::string("/proc/") + entry->d_name + "/fd";
          DIR* fddir = opendir(fd_dir.c_str());
          if (fddir) {
            dirent* fdentry;
            while ((fdentry = readdir(fddir)) != nullptr) {
              std::string linkpath = fd_dir + "/" + fdentry->d_name;
              std::array<char, 256> buf{};
              ssize_t len = readlink(linkpath.c_str(), buf.data(), buf.size() - 1);
              if (len > 0) {
                buf[static_cast<std::size_t>(len)] = '\0';
                std::string target(buf.data());
                for (const auto& inode : socket_inodes) {
                  if (target == "socket:[" + inode + "]") {
                    closedir(fddir);
                    closedir(proc);
                    return pid;
                  }
                }
              }
            }
            closedir(fddir);
          }
        }
      }
      closedir(proc);
    }
  }

  // Fallback using fuser
  auto res = ProcessRunner::run_sync({"fuser", std::to_string(port) + "/tcp"});
  if (res.exit_code == 0 && !res.stdout_text.empty()) {
    std::istringstream iss(res.stdout_text);
    int pid = 0;
    if (iss >> pid && pid > 0) return pid;
  }

  return 0;
#endif
}

bool BackendManager::stop_user_service(const std::string& unit_name) {
  if (unit_name.empty()) return true;
  ProcessRunner::run_sync({"systemctl", "--user", "stop", unit_name});
  ProcessRunner::run_sync({"systemctl", "--user", "reset-failed", unit_name});
  return true;
}

bool BackendManager::free_port(int port, int timeout_ms) {
  if (!is_port_listening(port)) return true;

  // Stop corresponding systemd service if any
  stop_user_service("paperboat-backend-" + std::to_string(port) + ".service");
  if (port == 3047) stop_user_service("paperboat-web.service");

  int pid = find_pid_by_port(port);
  if (pid > 0) {
    ProcessRunner::kill_pid(pid, SIGTERM, timeout_ms / 2);
  }

  // Fallback to fuser -k
  if (is_port_listening(port)) {
    ProcessRunner::run_sync({"fuser", "-k", std::to_string(port) + "/tcp"});
  }

  auto start = std::chrono::steady_clock::now();
  while (is_port_listening(port)) {
    auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now() - start);
    if (elapsed.count() >= timeout_ms) {
      int late_pid = find_pid_by_port(port);
      if (late_pid > 0) ProcessRunner::kill_pid(late_pid, SIGKILL, 200);
      return !is_port_listening(port);
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(20));
  }
  return true;
}

fs::path BackendManager::runtime_dir() {
  return platform::shared_tmp_dir() / "paperboat-runtime";
}

fs::path BackendManager::runtime_record_path(int port) {
  return runtime_dir() / ("backend-" + std::to_string(port) + ".json");
}

void BackendManager::register_running_backend(const RunningBackend& backend) {
  std::error_code ec;
  fs::create_directories(runtime_dir(), ec);
  const auto p = runtime_record_path(backend.port);

  json doc = {
      {"port", backend.port},
      {"pid", backend.pid},
      {"engine", backend.engine},
      {"domain", backend.domain},
      {"cwd", backend.cwd.string()},
      {"backend_dir", backend.backend_dir.string()},
      {"executable", backend.executable},
      {"ws_url", backend.ws_url.empty() ? ws_url(backend.port) : backend.ws_url},
      {"started_at", backend.started_at > 0 ? backend.started_at : current_timestamp()}};

  std::ofstream out(p);
  if (out) out << doc.dump(2) << '\n';
}

void BackendManager::unregister_running_backend(int port) {
  std::error_code ec;
  fs::remove(runtime_record_path(port), ec);
}

std::vector<RunningBackend> BackendManager::list_running_backends() {
  std::vector<RunningBackend> result;
  const auto rdir = runtime_dir();

  if (fs::is_directory(rdir)) {
    std::error_code ec;
    for (const auto& entry : fs::directory_iterator(rdir, ec)) {
      if (!entry.is_regular_file()) continue;
      const auto path = entry.path();
      if (!path.filename().string().starts_with("backend-") || path.extension() != ".json") continue;

      try {
        std::ifstream in(path);
        if (!in) continue;
        auto doc = json::parse(in);
        int port = doc.value("port", 0);
        int pid = doc.value("pid", 0);

        if (port <= 0 || pid <= 0 || !ProcessRunner::is_alive(pid) || !is_port_listening(port)) {
          // Prune stale file
          std::error_code rm_ec;
          fs::remove(path, rm_ec);
          continue;
        }

        RunningBackend b;
        b.port = port;
        b.pid = pid;
        b.engine = doc.value("engine", "");
        b.domain = doc.value("domain", "");
        b.cwd = doc.value("cwd", "");
        b.backend_dir = doc.value("backend_dir", "");
        b.executable = doc.value("executable", "");
        b.ws_url = doc.value("ws_url", ws_url(port));
        b.started_at = doc.value("started_at", int64_t{0});

        result.push_back(std::move(b));
      } catch (...) {
        std::error_code rm_ec;
        fs::remove(path, rm_ec);
      }
    }
  }

  std::ranges::sort(result, [](const RunningBackend& a, const RunningBackend& b) {
    return a.port < b.port;
  });
  return result;
}

}  // namespace paperboat::core
