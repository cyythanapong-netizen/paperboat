#include "paperboat/toolkit/scene.hpp"

#include <algorithm>
#include <cmath>
#include <utility>

namespace paperboat::toolkit::scene {

Entity box(std::string id, Vec pos, Vec size, std::string color, Vec rot) {
  return {.id = std::move(id), .shape = "box", .pos = pos, .size = size, .rot = rot, .color = std::move(color)};
}

Entity sphere(std::string id, Vec pos, double radius, std::string color) {
  return {.id = std::move(id), .shape = "sphere", .pos = pos, .size = {radius, radius, radius}, .color = std::move(color)};
}

Entity line(std::string id, Vec from, Vec to, std::string color) {
  return {.id = std::move(id), .shape = "line", .pos = from, .color = std::move(color), .to = to};
}

Entity image(std::string id, Vec pos, double width, double height, std::string src) {
  return {.id = std::move(id), .shape = "image", .pos = pos, .size = {width, height, 1}, .src = std::move(src)};
}

Entity rect(std::string id, double x, double y, double width, double height, std::string color, double angle, double z) {
  // Thin in depth: a card the 2D view sees face on.
  return box(std::move(id), {x, y, z}, {width, height, 0.02}, std::move(color), {0, 0, angle});
}

Entity circle(std::string id, double x, double y, double radius, std::string color, double z) {
  return sphere(std::move(id), {x, y, z}, radius, std::move(color));
}

Entity rod(std::string id, Vec a, Vec b, double thickness, std::string color) {
  const Vec d = {b[0] - a[0], b[1] - a[1], b[2] - a[2]};
  const double length = std::sqrt(d[0] * d[0] + d[1] * d[1] + d[2] * d[2]);
  const Vec mid = {(a[0] + b[0]) / 2, (a[1] + b[1]) / 2, (a[2] + b[2]) / 2};
  // A box along x, turned about z (up out of the ground plane), then about y (round in it):
  // the scene applies z first, then y, then x.
  const double up = length > 0 ? std::asin(std::clamp(d[1] / length, -1.0, 1.0)) : 0;
  const double round = std::atan2(-d[2], d[0]);
  return box(std::move(id), mid, {length, thickness, thickness}, std::move(color), {0, round, up});
}

std::vector<Entity> spring(const std::string& id, Vec a, Vec b, int coils, double width, const std::string& color) {
  coils = std::max(1, coils);
  const Vec d = {b[0] - a[0], b[1] - a[1], b[2] - a[2]};
  const double length = std::sqrt(d[0] * d[0] + d[1] * d[1] + d[2] * d[2]);
  // Across the spring, in the x–y plane (a vertical spring zigzags along x).
  Vec across = length > 0 ? Vec{-d[1] / length, d[0] / length, 0} : Vec{0, 1, 0};
  if (std::abs(across[0]) + std::abs(across[1]) < 1e-9) across = {1, 0, 0};
  const int points = 2 * coils + 2;  // the ends and one point each side per coil
  auto at = [&](int i) {
    const double f = static_cast<double>(i) / (points - 1);
    const double side = (i == 0 || i == points - 1) ? 0 : (i % 2 ? 0.5 : -0.5) * width;
    return Vec{a[0] + f * d[0] + side * across[0], a[1] + f * d[1] + side * across[1], a[2] + f * d[2] + side * across[2]};
  };
  std::vector<Entity> out;
  for (int i = 0; i + 1 < points; ++i) out.push_back(line(id + "/" + std::to_string(i), at(i), at(i + 1), color));
  return out;
}

}  // namespace paperboat::toolkit::scene
