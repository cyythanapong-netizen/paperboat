#pragma once

// Building a scene: entities by what they are, not by their fields. Each returns the
// protocol's Entity (station.hpp), so a station object's or domain state's entities() is a
// list of calls:
//
//   return {paperboat::scene::rect(id + "/cart", x, 0, 0.6, 0.3, "#2f5d62"),
//           paperboat::scene::rod(id + "/pole", {x, 0.15, 0}, tip, 0.04, "#1c2321"),
//           paperboat::scene::circle(id + "/bob", tip[0], tip[1], 0.08, "#c2410c")};
//
// Units are the scene's (metres in the examples); y is up. The 2D helpers (rect, circle)
// lie in the x–y plane at depth z, which is what the 2D view ({"mode": "2d"}) shows.
// Colours are CSS colours ("#2f5d62").

#include <array>
#include <string>
#include <vector>

#include "paperboat/toolkit/station.hpp"

namespace paperboat::toolkit::scene {

using Vec = std::array<double, 3>;

// A box centred at `pos`, turned by `rot` (radians, about x, then y, then z).
Entity box(std::string id, Vec pos, Vec size, std::string color = "#888888", Vec rot = {});
// A ball centred at `pos`.
Entity sphere(std::string id, Vec pos, double radius, std::string color = "#888888");
// A straight line from `from` to `to`.
Entity line(std::string id, Vec from, Vec to, std::string color = "#888888");
// A picture `width` × `height` facing +z, centred at `pos`: a URL or a data: URL.
Entity image(std::string id, Vec pos, double width, double height, std::string src);

// A rectangle in the x–y plane centred at (x, y), turned by `angle` (radians, anticlockwise).
Entity rect(std::string id, double x, double y, double width, double height, std::string color = "#888888",
            double angle = 0, double z = 0);
// A disc in the x–y plane centred at (x, y).
Entity circle(std::string id, double x, double y, double radius, std::string color = "#888888", double z = 0);

// A bar of square section from `a` to `b` (a link, a pole, a strut).
Entity rod(std::string id, Vec a, Vec b, double thickness, std::string color = "#888888");
// A coil spring from `a` to `b`: a zigzag of lines `width` across, ids `id/0`, `id/1`…
// The same number of lines whatever its length, so a patch updates them all.
std::vector<Entity> spring(const std::string& id, Vec a, Vec b, int coils = 8, double width = 0.2,
                           const std::string& color = "#888888");

}  // namespace paperboat::toolkit::scene
