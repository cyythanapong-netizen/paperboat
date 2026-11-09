#pragma once

// Shared by the list blocks (#24): a list of numbers is a Vector on a "numbers" port, a list
// of points a Table (one row per point) on a "points" port.

#include <cmath>
#include <cstddef>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

#include "common.hpp"
#include "paperboat/toolkit/bases.hpp"

namespace paperboat::toolkit::list {

// The most items a block makes: a Range of ten million is a slip, not a list.
inline constexpr std::size_t kMaxItems = 1'000'000;

// A count param: a whole number from 0 to kMaxItems.
inline std::size_t count_param(const Values& params, std::string_view id) {
  const double n = get_or(params, id);
  if (!std::isfinite(n) || n < 0 || n != std::floor(n) || n > static_cast<double>(kMaxItems))
    throw std::invalid_argument(std::string(id) + " must be a whole number from 0 to " + std::to_string(kMaxItems));
  return static_cast<std::size_t>(n);
}

// The points at `id`: a table as it is; anything else (unwired, an empty list) none.
inline Table points_at(const Values& in, std::string_view id) {
  const auto* table = get_if<Table>(in, id);
  return table ? *table : Table{};
}

// The two forms of a block that takes a list `list`: "numbers" (a list of numbers in, `numbers_out`
// out) and "points" (a list of points in, `points_out` out). Its `form` param picks one.
inline std::vector<Form> list_forms(const std::vector<PortDef>& more_inputs, std::vector<PortDef> numbers_out,
                                    std::vector<PortDef> points_out) {
  std::vector<PortDef> numbers_in{numbers_port("list")}, points_in{points_port("list")};
  numbers_in.insert(numbers_in.end(), more_inputs.begin(), more_inputs.end());
  points_in.insert(points_in.end(), more_inputs.begin(), more_inputs.end());
  return {{.name = "numbers", .inputs = std::move(numbers_in), .outputs = std::move(numbers_out)},
          {.name = "points", .inputs = std::move(points_in), .outputs = std::move(points_out)}};
}

// True when the block is in its points form.
inline bool points_form(const Config& config) {
  const auto it = config.find("form");
  return it != config.end() && it->second == "points";
}

// Item `index` of a list of `n`: 0 is the first, -1 the last. Throws past either end.
inline std::size_t at_index(double index, std::size_t n) {
  if (!std::isfinite(index) || index != std::floor(index))
    throw std::invalid_argument("index must be a whole number, not " + value_to_text(index));
  const double i = index < 0 ? index + static_cast<double>(n) : index;
  if (n == 0) throw std::invalid_argument("the list is empty: there is no item " + value_to_text(index));
  if (i < 0 || i >= static_cast<double>(n))
    throw std::invalid_argument("index " + value_to_text(index) + " is outside a list of " + std::to_string(n) +
                                " items (0 to " + std::to_string(n - 1) + ", or -1 for the last)");
  return static_cast<std::size_t>(i);
}

}  // namespace paperboat::toolkit::list
