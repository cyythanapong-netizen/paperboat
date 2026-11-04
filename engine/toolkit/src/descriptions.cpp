// One line each, in plain words, for the built-in templates: what the block (or station
// object) does, for its card in the browser. A domain's own blocks set BlockDef::description
// (or `description` in TOML).

#include <map>
#include <string>
#include <string_view>

#include "common.hpp"

namespace paperboat::toolkit {

std::string builtin_description(std::string_view name) {
  static const std::map<std::string_view, std::string_view> text = {
      // base
      {"base/constant", "A number you set."},
      {"base/text", "A piece of text you set."},
      {"base/boolean", "On or off: 1 or 0."},
      {"base/key", "1 while a key is held down in the app."},
      {"base/if", "Picks a or b by a condition; on a Step flow, branches the path."},
      {"base/and", "Holds when a and b both hold."},
      {"base/or", "Holds when a or b holds."},
      {"base/not", "Holds when its input doesn't."},
      {"base/compare", "Compares a with b: greater, less, equal…"},
      // signal
      {"signal/clock", "The run's time, in seconds."},
      // math
      {"math/gain", "Multiplies its input by k."},
      {"math/sum", "Adds a and b."},
      {"math/threshold", "1 above a level, else 0."},
      {"math/saturation", "Keeps its input between a minimum and a maximum."},
      // control
      {"control/pid", "Drives an error to zero: proportional, integral and derivative terms."},
      {"control/integrator", "Accumulates its input over time. A feedback loop may pass through it."},
      {"control/lag", "Smooths its input with a time constant: a first-order lag."},
      {"control/delay", "Its input from a number of clock steps ago; any value."},
      // dashboard
      {"dashboard/graph", "Plots its input over time on the Dashboard."},
      {"dashboard/display", "Shows its latest value on the Dashboard."},
      // step
      {"step/start", "Starts a path once, when the run begins."},
      {"step/when", "Starts a path each time its condition rises (or falls): a key, a threshold."},
      {"step/wait", "Holds the path for a while."},
      {"step/repeat", "Leaves by loop a number of times, then by done; count is the lap."},
      {"step/while", "Leaves by loop while its condition holds, then by done."},
      {"step/say", "Writes a line to the Console as the path passes: its text and a value."},
      {"step/join", "Waits for a number of paths to arrive, then goes on as one."},
      {"step/stop", "Ends the whole run."},
      // opencv
      {"opencv/gray", "Turns an image to grey."},
      {"opencv/blur", "Softens an image."},
      {"opencv/median", "Removes speckle noise, keeping edges."},
      {"opencv/threshold", "Splits an image into black and white at a level."},
      {"opencv/adaptive_threshold", "Black and white by local brightness: for uneven light."},
      {"opencv/hsv_range", "Keeps the pixels within a range of colour."},
      {"opencv/canny", "Finds edges."},
      {"opencv/erode", "Shrinks white areas."},
      {"opencv/dilate", "Grows white areas."},
      {"opencv/open", "Removes small white specks."},
      {"opencv/close", "Fills small black holes."},
      {"opencv/invert", "Swaps black and white."},
      {"opencv/distance", "How far each white pixel is from black."},
      {"opencv/view", "Shows an image on the Dashboard."},
      {"opencv/image", "A picture from a file, for a flow to work on."},
  };
  auto it = text.find(name);
  return it == text.end() ? std::string() : std::string(it->second);
}

View builtin_view(std::string_view name) {
  // Simple maths and logic blocks are small plates (#70): a glyph, and a caption with the
  // main value. Everything else is drawn as the normal card.
  static const std::map<std::string_view, View> views = {
      {"base/constant", {.compact = true, .glyph = "const", .value = "{value}"}},
      {"base/and", {.compact = true, .glyph = "&"}},
      {"base/or", {.compact = true, .glyph = "≥1"}},
      {"base/not", {.compact = true, .glyph = "¬"}},
      {"base/compare", {.compact = true, .glyph = "cmp", .value = "{op}"}},
      {"math/gain", {.compact = true, .glyph = "gain", .value = "{k}"}},
      {"math/sum", {.compact = true, .glyph = "plus"}},
      {"math/threshold", {.compact = true, .glyph = "step", .value = "{level}"}},
      {"math/saturation", {.compact = true, .glyph = "clamp", .value = "{min}…{max}"}},
  };
  auto it = views.find(name);
  return it == views.end() ? View{} : it->second;
}

}  // namespace paperboat::toolkit
