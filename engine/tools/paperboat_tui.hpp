#pragma once

#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>
#include <string_view>
#include <thread>
#include <unistd.h>
#include <vector>

#include "paperboat/core/backend_manager.hpp"
#include "paperboat/core/process.hpp"

namespace paperboat::tui {

namespace fs = std::filesystem;

inline std::string get_git_info() {
  auto res = core::ProcessRunner::run_sync({"git", "rev-parse", "--abbrev-ref", "HEAD"});
  auto hash_res = core::ProcessRunner::run_sync({"git", "rev-parse", "--short", "HEAD"});

  std::string branch = res.ok() ? res.stdout_text : "";
  while (!branch.empty() && (branch.back() == '\n' || branch.back() == '\r' || branch.back() == ' ')) branch.pop_back();

  std::string hash = hash_res.ok() ? hash_res.stdout_text : "";
  while (!hash.empty() && (hash.back() == '\n' || hash.back() == '\r' || hash.back() == ' ')) hash.pop_back();

  auto tag_res = core::ProcessRunner::run_sync({"git", "describe", "--tags", "--exact-match"});
  std::string tag = tag_res.ok() ? tag_res.stdout_text : "";
  while (!tag.empty() && (tag.back() == '\n' || tag.back() == '\r' || tag.back() == ' ')) tag.pop_back();

  if (!tag.empty()) {
    return tag + (hash.empty() ? "" : " (" + hash + ")");
  }
  if (!branch.empty() && !hash.empty()) {
    return branch + " @ " + hash;
  }
  if (!hash.empty()) {
    return hash;
  }
  return "release 0.1.0";
}

inline std::string get_compact_cwd() {
  std::string cwd = fs::current_path().string();
  const char* home = std::getenv("HOME");
  if (home && *home) {
    std::string home_str(home);
    if (cwd.starts_with(home_str)) {
      cwd = "~" + cwd.substr(home_str.size());
    }
  }
  return cwd;
}

inline void render_banner(bool animated = true) {
  const bool is_tty = isatty(STDOUT_FILENO) && isatty(STDIN_FILENO);

  const std::string INK = is_tty ? "\033[38;2;233;233;228m" : "";
  const std::string SHADE = is_tty ? "\033[38;2;141;132;158m" : "";
  const std::string ORANGE = is_tty ? "\033[38;2;255;106;48m" : "";
  const std::string BLUE = is_tty ? "\033[1;38;2;74;144;226m" : "";
  const std::string GRAY = is_tty ? "\033[38;2;140;140;140m" : "";
  const std::string DIM_ORANGE = is_tty ? "\033[38;2;160;65;30m" : "";
  const std::string BRIGHT_ORANGE = is_tty ? "\033[1;38;2;255;150;90m" : "";
  const std::string RST = is_tty ? "\033[0m" : "";

  std::string git_info = get_git_info();
  std::string cwd_info = get_compact_cwd();

  const std::vector<std::string> logo_lines = {
      "        " + INK + "█" + SHADE + "█" + RST + "        ",
      "      " + INK + "███" + SHADE + "███" + RST + "      ",
      "    " + INK + "█████" + SHADE + "█████" + RST + "    ",
      "  " + INK + "▄██████████████▄" + RST,
      "   " + INK + "▀████████████▀" + RST + " ",
  };

  const std::vector<std::string> text_lines = {
      BLUE + "Paperboat CLI 0.1.0" + RST,
      GRAY + git_info + RST,
      GRAY + "C++20 core · protocol paperboat/0" + RST,
      GRAY + cwd_info + RST,
      "",
  };

  for (std::size_t i = 0; i < logo_lines.size(); ++i) {
    std::cout << "  " << logo_lines[i] << "   " << text_lines[i] << '\n';
  }

  // Water line with ripple animation
  auto make_water = [&](int active_dot) {
    std::string w = "   ";
    for (int dot = 0; dot < 5; ++dot) {
      if (dot > 0) w += "  ";
      if (active_dot == -1) {
        w += ORANGE + "█" + RST;
      } else if (dot == active_dot) {
        w += BRIGHT_ORANGE + "█" + RST;
      } else {
        w += DIM_ORANGE + "█" + RST;
      }
    }
    w += "  ";
    return w;
  };

  if (is_tty && animated) {
    std::cout << "  " << make_water(-1) << "\n" << std::flush;
    for (int step = 0; step < 5; ++step) {
      std::cout << "\033[1A\r  " << make_water(step) << "\n" << std::flush;
      std::this_thread::sleep_for(std::chrono::milliseconds(50));
    }
    std::cout << "\033[1A\r  " << make_water(-1) << "\n\n" << std::flush;
  } else {
    std::cout << "  " << make_water(-1) << "\n\n" << std::flush;
  }
}

inline std::vector<std::string> parse_command_line(std::string_view line) {
  std::vector<std::string> tokens;
  std::string cur;
  bool in_quotes = false;
  char quote_char = '\0';

  for (std::size_t i = 0; i < line.size(); ++i) {
    char c = line[i];
    if (in_quotes) {
      if (c == quote_char) {
        in_quotes = false;
      } else {
        cur += c;
      }
    } else {
      if (c == '"' || c == '\'') {
        in_quotes = true;
        quote_char = c;
      } else if (c == ' ' || c == '\t' || c == '\r' || c == '\n') {
        if (!cur.empty()) {
          tokens.push_back(std::move(cur));
          cur.clear();
        }
      } else {
        cur += c;
      }
    }
  }
  if (!cur.empty()) tokens.push_back(std::move(cur));
  return tokens;
}

}  // namespace paperboat::tui
