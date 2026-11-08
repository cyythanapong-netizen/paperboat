#include "paperboat_tui.hpp"

#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <iostream>

#include "paperboat/core/process.hpp"
#include "paperboat/toolkit/platform.hpp"

namespace paperboat::tui {

namespace fs = std::filesystem;

#ifndef PAPERBOAT_VERSION  // set by engine/tools/CMakeLists.txt from the project version
#define PAPERBOAT_VERSION "0.0.0-dev"
#endif
const char* const kVersion = PAPERBOAT_VERSION;

std::string get_git_info() {
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
  return std::string("release ") + kVersion;
}

std::string get_compact_cwd() {
  std::string cwd = fs::current_path().string();
  const std::string home = platform::home_dir().string();
  if (!home.empty()) {
    std::string home_str(home);
    if (cwd.starts_with(home_str)) {
      cwd = "~" + cwd.substr(home_str.size());
    }
  }
  return cwd;
}

void render_banner(bool animated) {
  const bool is_tty = platform::is_tty(1) && platform::is_tty(0);

  const std::string W = is_tty ? "\033[38;2;233;233;228m" : "";
  const std::string S = is_tty ? "\033[38;2;163;163;157m" : "";
  const std::string ORANGE = is_tty ? "\033[38;2;255;106;48m" : "";
  const std::string BRIGHT_ORANGE = is_tty ? "\033[1;38;2;255;160;100m" : "";
  const std::string BLUE = is_tty ? "\033[1;38;2;74;144;226m" : "";
  const std::string GRAY = is_tty ? "\033[38;2;140;140;140m" : "";
  const std::string RST = is_tty ? "\033[0m" : "";

  std::string git_info = get_git_info();
  std::string cwd_info = get_compact_cwd();

  // The 8-line compact brand boat (23 columns wide)
  const std::vector<std::string> logo_lines = {
      "          " + W + "▄█" + S + "▄" + RST + "          ",
      "        " + W + "▄███" + S + "██▄" + RST + "        ",
      "      " + W + "▄█████" + S + "████▄" + RST + "      ",
      W + "▄▄▄▄▄▄▄▄▄▄▄▄▄▄▄▄▄▄▄▄▄▄▄" + RST,
      " " + W + "▀███████████████████▀" + RST + " ",
      "   " + W + "▀███████████████▀" + RST + "   ",
  };

  const std::vector<std::string> text_lines = {
      BLUE + "Paperboat CLI " + kVersion + RST,
      GRAY + git_info + RST,
      GRAY + "C++20 core · protocol paperboat/0" + RST,
      GRAY + cwd_info + RST,
      "",
      "",
  };

  for (std::size_t i = 0; i < logo_lines.size(); ++i) {
    std::cout << "  " << logo_lines[i] << "   " << text_lines[i] << '\n';
  }

  // Water lines (Line 6: water gap / wave crest; Line 7: water dots)
  // 5 substantial dots (3 chars wide each) with 2-char gaps across 23 columns
  // to match the authentic brand pixel boat without appearing like thin sticks in monospace fonts.
  auto render_water_lines = [&](int active_dot) {
    std::string out6 = "  ";
    std::string out7 = "  ";

    for (int d = 0; d < 5; ++d) {
      if (d > 0) {
        out6 += "  ";
        out7 += "  ";
      }
      if (d == active_dot) {
        out6 += BRIGHT_ORANGE + "▄▄▄" + RST;
        out7 += BRIGHT_ORANGE + "▀▀▀" + RST;
      } else {
        out6 += "   ";
        out7 += ORANGE + "███" + RST;
      }
    }
    out6 += "   \n";
    out7 += "   \n";
    return std::pair{out6, out7};
  };

  auto [w6, w7] = render_water_lines(-1);

  if (is_tty && animated) {
    std::cout << w6 << w7 << std::flush;
    for (int step = 0; step < 5; ++step) {
      auto [u6, u7] = render_water_lines(step);
      std::cout << "\033[2A\r" << u6 << u7 << std::flush;
      std::this_thread::sleep_for(std::chrono::milliseconds(80));
    }
    // Return to rest
    std::cout << "\033[2A\r" << w6 << w7 << "\n" << std::flush;
  } else {
    std::cout << w6 << w7 << "\n" << std::flush;
  }
}

void WaveSpinner::start(std::string msg) {
  if (!msg.empty()) message_ = std::move(msg);
  if (running_ || !platform::is_tty(1)) return;
  running_ = true;
  thread_ = std::thread([this]() {
    int step = 0;
    const std::string ORANGE = "\033[38;2;255;106;48m";
    const std::string BRIGHT = "\033[1;38;2;255;160;100m";
    const std::string RST = "\033[0m";

    while (running_) {
      std::string frame = "  ";
      for (int d = 0; d < 5; ++d) {
        if (d > 0) frame += " ";
        if (d == step) {
          frame += BRIGHT + "▀▀" + RST;
        } else {
          frame += ORANGE + "▄▄" + RST;
        }
      }
      std::cout << "\r" << frame << "  " << message_ << "   " << std::flush;
      step = (step + 1) % 5;
      std::this_thread::sleep_for(std::chrono::milliseconds(90));
    }
  });
}

void WaveSpinner::stop(bool ok, std::string_view final_msg) {
  if (!running_) return;
  running_ = false;
  if (thread_.joinable()) thread_.join();
  if (platform::is_tty(1)) {
    std::cout << "\r\033[2K";
    if (!final_msg.empty()) {
      std::cout << (ok ? "  \033[32m✓\033[0m " : "  \033[31m✗\033[0m ") << final_msg << '\n';
    }
    std::cout << std::flush;
  }
}

std::vector<std::string> parse_command_line(std::string_view line) {
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
