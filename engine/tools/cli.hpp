#pragma once

// Shared by the paperboat CLI's files: paperboat_cli.cpp (main, the command table, help), cli_backends.cpp
// (the backend commands) and cli_pkg.cpp (apps, packages, updates and toolkits).

#include <algorithm>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <optional>
#include <iostream>
#include <map>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include <nlohmann/json.hpp>

#include "paperboat/core/backend_manager.hpp"
#include "paperboat/core/process.hpp"
#include "paperboat/toolkit/apps.hpp"
#include "paperboat/toolkit/descriptor.hpp"
#include "paperboat/toolkit/platform.hpp"
#include "paperboat/toolkit/update.hpp"
#include "paperboat/toolkit/installer.hpp"
#ifdef PAPERBOAT_HAS_OPENCV
#include "paperboat/toolkit/opencv.hpp"
#endif
#include "examples.hpp"
#include "paperboat_tui.hpp"

namespace paperboat::cli {

using nlohmann::json;
namespace core = paperboat::core;
namespace tk = paperboat::toolkit;
namespace tui = paperboat::tui;
namespace platform = paperboat::platform;
namespace fs = std::filesystem;

using Command = int (*)(const std::vector<std::string_view>&);

// The command table, in paperboat_cli.cpp.
Command find_command(std::string_view name);
// Opens the interactive console on `args`; false when it cannot (not built, not a terminal).
bool launch_console(const std::vector<std::string>& args);
std::string target_of(const std::vector<std::string_view>& args);
std::vector<std::string_view> without(const std::vector<std::string_view>& args, std::string_view target);

// Backend commands, in cli_backends.cpp.
int cmd_run(const std::vector<std::string_view>& args);
int cmd_open(const std::vector<std::string_view>& args);
int cmd_build(const std::vector<std::string_view>& args);
int cmd_rebuild(const std::vector<std::string_view>& args);
int cmd_backends(const std::vector<std::string_view>& args);
int cmd_list(const std::vector<std::string_view>& args);
int cmd_restart(const std::vector<std::string_view>& args);
int cmd_stop(const std::vector<std::string_view>& args);
int cmd_start(const std::vector<std::string_view>& args);
int cmd_check(const std::vector<std::string_view>& args);
int cmd_pack(const std::vector<std::string_view>& args);
int cmd_unpack(const std::vector<std::string_view>& args);

// Programs, apps, updates and toolkits, in cli_pkg.cpp.
int cmd_app(const std::vector<std::string_view>& args);
int cmd_toolkit(const std::vector<std::string_view>& args);
int cmd_pkg(const std::vector<std::string_view>& args);
int cmd_update(const std::vector<std::string_view>& args);
std::optional<tk::AppEntry> find_program(std::string_view target);
int main_app(std::string_view verb, const std::vector<std::string_view>& args);
int stop_program(tk::AppEntry app, const std::vector<std::string_view>& args = {});
int start_program(const tk::AppEntry& app, std::vector<std::string_view> rest);

}  // namespace paperboat::cli
