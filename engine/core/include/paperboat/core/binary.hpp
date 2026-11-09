#pragma once

// Binary frames (protocol/README.md, "Binary frames"): how a typed array (toolkit::Array: a
// mesh, a field) leaves the engine. Inside the engine a message carries an array as
// array_json(); just before it is sent, lower_arrays() turns each one into a {"binary": id}
// reference plus a binary WebSocket frame for a client that asked for them, or into a flat JSON
// array of numbers for any other (and for --run's JSON lines).
//
// Frame layout (version 1), every integer little-endian:
//
//   offset 0   4 bytes  magic "PBIN"
//          4   u8       version (1)
//          5   u8       flags (0; a decoder refuses others)
//          6   u16      reserved (0)
//          8   u32      H: the descriptor's length in bytes
//         12   H bytes  descriptor: UTF-8 JSON {"id", "dtype", "shape", "path"?, "compression"?}
//    12 + H   0-7 bytes zero padding, so the payload starts at a multiple of 8
//         P   the rest  payload: the elements, little-endian, row-major (product(shape) of dtype)

#include <cstdint>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include <nlohmann/json.hpp>

#include "paperboat/toolkit/array.hpp"

namespace paperboat::core {

using json = nlohmann::json;

inline constexpr int kFrameVersion = 1;  // the layout above; hello.binary names it
inline constexpr std::string_view kFrameMagic = "PBIN";

// An array as a message carries it inside the engine: {"dtype", "shape", "data": <json binary>}.
json array_json(const toolkit::Array& array);
bool is_array_json(const json& value);
// Back from array_json(); throws std::invalid_argument for anything else.
toolkit::Array array_from_json(const json& value);

// One frame: the header, `descriptor` and `payload` laid out as above.
std::string encode_frame(const json& descriptor, std::span<const std::uint8_t> payload);

struct Frame {
  json descriptor;
  toolkit::Array array;  // the payload as the descriptor's dtype and shape
};
// Reads a frame; throws std::invalid_argument when it isn't one (bad magic, version, flags,
// lengths, an unknown dtype or compression).
Frame decode_frame(std::string_view bytes);

// True when `message` carries an array_json() anywhere (what lower_arrays() changes).
bool has_arrays(const json& message);

// `message` ready to send. With `frames` null, every array becomes a flat JSON array of
// numbers. Otherwise each becomes {"binary": "<id>"} and its frame is appended to `frames`, in
// order: the frames go out right before the message. Ids count up from `*next_id`.
json lower_arrays(const json& message, std::vector<std::string>* frames = nullptr, std::uint64_t* next_id = nullptr);

}  // namespace paperboat::core
