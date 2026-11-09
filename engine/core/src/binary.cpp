#include "paperboat/core/binary.hpp"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <stdexcept>

namespace paperboat::core {

namespace {

namespace tk = toolkit;

constexpr std::size_t kHeader = 12;

void put_u32(std::string& out, std::size_t at, std::uint32_t v) {
  for (std::size_t i = 0; i < 4; ++i) out[at + i] = static_cast<char>((v >> (8 * i)) & 0xff);
}

std::uint32_t get_u32(std::string_view in, std::size_t at) {
  std::uint32_t v = 0;
  for (std::size_t i = 0; i < 4; ++i) v |= static_cast<std::uint32_t>(static_cast<unsigned char>(in[at + i])) << (8 * i);
  return v;
}

// An f32 as the shortest decimal that reads back as the same float, so 0.1f is 0.1 in JSON
// (not 0.10000000149011612).
double short_float(float v) {
  char buf[32];
  for (int precision = 6; precision < 9; ++precision) {
    std::snprintf(buf, sizeof buf, "%.*g", precision, static_cast<double>(v));
    if (std::strtof(buf, nullptr) == v) return std::strtod(buf, nullptr);
  }
  return v;
}

// The flat JSON array a client without binary frames gets.
json numbers(const tk::Array& a) {
  json out = json::array();
  out.get_ref<json::array_t&>().reserve(a.size());
  for (std::size_t i = 0; i < a.size(); ++i) {
    const double v = a.at(i);
    switch (a.dtype()) {
      case tk::DType::F32: out.push_back(short_float(static_cast<float>(v))); break;
      case tk::DType::F64: out.push_back(v); break;
      case tk::DType::I32: out.push_back(static_cast<std::int64_t>(v)); break;
      case tk::DType::U32: case tk::DType::U8: out.push_back(static_cast<std::uint64_t>(v)); break;
    }
  }
  return out;
}

std::string escape_pointer(const std::string& key) {
  std::string out;
  for (char c : key) {
    if (c == '~') out += "~0";
    else if (c == '/') out += "~1";
    else out += c;
  }
  return out;
}

json lower(const json& value, const std::string& path, std::vector<std::string>* frames, std::uint64_t* next_id) {
  if (is_array_json(value)) {
    const auto array = array_from_json(value);
    if (!frames) return numbers(array);
    const std::string id = std::to_string((*next_id)++);
    json descriptor = {{"id", id}, {"dtype", tk::dtype_name(array.dtype())}, {"shape", array.shape()}};
    if (!path.empty()) descriptor["path"] = path;
    frames->push_back(encode_frame(descriptor, array.bytes()));
    return {{"binary", id}};
  }
  if (value.is_object()) {
    json out = json::object();
    for (const auto& [k, v] : value.items()) out[k] = lower(v, path + "/" + escape_pointer(k), frames, next_id);
    return out;
  }
  if (value.is_array()) {
    json out = json::array();
    std::size_t i = 0;
    for (const auto& v : value) out.push_back(lower(v, path + "/" + std::to_string(i++), frames, next_id));
    return out;
  }
  return value;
}

}  // namespace

json array_json(const tk::Array& array) {
  const auto bytes = array.bytes();
  return {{"dtype", tk::dtype_name(array.dtype())},
          {"shape", array.shape()},
          {"data", json::binary(std::vector<std::uint8_t>(bytes.begin(), bytes.end()))}};
}

bool is_array_json(const json& value) {
  if (!value.is_object() || value.size() != 3) return false;
  const auto data = value.find("data");
  return data != value.end() && data->is_binary() && value.contains("dtype") && value.contains("shape");
}

tk::Array array_from_json(const json& value) {
  if (!is_array_json(value)) throw std::invalid_argument("not an array");
  const auto dtype = tk::dtype_from(value.at("dtype").get<std::string>());
  if (!dtype) throw std::invalid_argument("unknown dtype " + value.at("dtype").dump());
  const auto& bytes = value.at("data").get_binary();
  return tk::Array::from_le_bytes(*dtype, value.at("shape").get<std::vector<std::size_t>>(), bytes);
}

std::string encode_frame(const json& descriptor, std::span<const std::uint8_t> payload) {
  const std::string text = descriptor.dump();
  const std::size_t start = (kHeader + text.size() + 7) / 8 * 8;
  std::string out(start + payload.size(), '\0');
  std::memcpy(out.data(), kFrameMagic.data(), 4);
  out[4] = static_cast<char>(kFrameVersion);
  put_u32(out, 8, static_cast<std::uint32_t>(text.size()));
  std::memcpy(out.data() + kHeader, text.data(), text.size());
  if (!payload.empty()) std::memcpy(out.data() + start, payload.data(), payload.size());
  return out;
}

Frame decode_frame(std::string_view bytes) {
  if (bytes.size() < kHeader || bytes.substr(0, 4) != kFrameMagic) throw std::invalid_argument("not a Paperboat binary frame");
  if (bytes[4] != kFrameVersion) throw std::invalid_argument("binary frame version " + std::to_string(+bytes[4]) + " is not 1");
  if (bytes[5] != 0 || bytes[6] != 0 || bytes[7] != 0) throw std::invalid_argument("binary frame flags must be 0");
  const std::size_t length = get_u32(bytes, 8);
  if (kHeader + length > bytes.size()) throw std::invalid_argument("binary frame descriptor runs past the frame");
  Frame frame;
  frame.descriptor = json::parse(bytes.substr(kHeader, length), nullptr, /*allow_exceptions=*/false);
  if (!frame.descriptor.is_object() || !frame.descriptor.contains("id") || !frame.descriptor.contains("shape"))
    throw std::invalid_argument("binary frame descriptor is not {id, dtype, shape}");
  if (const auto c = frame.descriptor.value("compression", std::string("none")); c != "none")
    throw std::invalid_argument("binary frame compression " + c + " is not supported");
  const auto dtype = tk::dtype_from(frame.descriptor.value("dtype", std::string()));
  if (!dtype) throw std::invalid_argument("binary frame dtype " + frame.descriptor.value("dtype", std::string()) + " is unknown");
  const std::size_t start = (kHeader + length + 7) / 8 * 8;
  if (start > bytes.size()) throw std::invalid_argument("binary frame is shorter than its padding");
  const auto* payload = reinterpret_cast<const std::uint8_t*>(bytes.data()) + start;
  frame.array = tk::Array::from_le_bytes(*dtype, frame.descriptor.at("shape").get<std::vector<std::size_t>>(),
                                         {payload, bytes.size() - start});
  return frame;
}

bool has_arrays(const json& message) {
  if (is_array_json(message)) return true;
  if (message.is_object() || message.is_array())
    for (const auto& v : message) if (has_arrays(v)) return true;
  return false;
}

json lower_arrays(const json& message, std::vector<std::string>* frames, std::uint64_t* next_id) {
  std::uint64_t own = 1;
  return lower(message, "", frames, next_id ? next_id : &own);
}

}  // namespace paperboat::core
