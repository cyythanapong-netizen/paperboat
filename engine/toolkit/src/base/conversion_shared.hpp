#pragma once

// Shared by several base blocks.

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdlib>
#include <string>
#include <string_view>

#include "common.hpp"

namespace paperboat::toolkit::base {

inline std::string trim_spaces(std::string_view s) {
  while (!s.empty() && std::isspace(static_cast<unsigned char>(s.front()))) s.remove_prefix(1);
  while (!s.empty() && std::isspace(static_cast<unsigned char>(s.back()))) s.remove_suffix(1);
  return std::string(s);
}

inline std::string to_lower_trimmed(std::string_view s) {
  std::string out = trim_spaces(s);
  for (char& c : out) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
  return out;
}

}  // namespace paperboat::toolkit::base
