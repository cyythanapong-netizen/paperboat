#include "paperboat/toolkit/array.hpp"

#include <algorithm>
#include <bit>
#include <cstring>
#include <functional>
#include <numeric>
#include <stdexcept>
#include <string>

namespace paperboat::toolkit {

namespace {

// Reverses each element's bytes in place: native to little-endian, or back, on a big-endian host.
void swap_elements(std::vector<std::uint8_t>& bytes, std::size_t width) {
  if constexpr (std::endian::native == std::endian::little) return;
  if (width == 1) return;
  for (std::size_t i = 0; i + width <= bytes.size(); i += width) std::reverse(bytes.data() + i, bytes.data() + i + width);
}

std::vector<std::size_t> checked_shape(std::vector<std::size_t> shape, std::size_t count) {
  if (shape.empty()) return {count};
  const std::size_t n = std::accumulate(shape.begin(), shape.end(), std::size_t{1}, std::multiplies<>());
  if (n != count)
    throw std::invalid_argument("an array's shape holds " + std::to_string(n) + " elements, not " + std::to_string(count));
  return shape;
}

}  // namespace

std::string_view dtype_name(DType dtype) {
  switch (dtype) {
    case DType::F32: return "f32";
    case DType::F64: return "f64";
    case DType::I32: return "i32";
    case DType::U32: return "u32";
    case DType::U8: return "u8";
  }
  return "f64";
}

std::size_t dtype_size(DType dtype) {
  switch (dtype) {
    case DType::F32: case DType::I32: case DType::U32: return 4;
    case DType::F64: return 8;
    case DType::U8: return 1;
  }
  return 8;
}

std::optional<DType> dtype_from(std::string_view name) {
  for (DType d : {DType::F32, DType::F64, DType::I32, DType::U32, DType::U8})
    if (dtype_name(d) == name) return d;
  return std::nullopt;
}

Array::Array(DType dtype, std::vector<std::size_t> shape, const void* data, std::size_t count)
    : dtype_(dtype), shape_(checked_shape(std::move(shape), count)) {
  std::vector<std::uint8_t> bytes(count * dtype_size(dtype));
  if (!bytes.empty()) std::memcpy(bytes.data(), data, bytes.size());
  swap_elements(bytes, dtype_size(dtype));
  data_ = std::make_shared<const std::vector<std::uint8_t>>(std::move(bytes));
}

Array Array::from_le_bytes(DType dtype, std::vector<std::size_t> shape, std::span<const std::uint8_t> bytes) {
  if (bytes.size() % dtype_size(dtype) != 0)
    throw std::invalid_argument("an array of " + std::string(dtype_name(dtype)) + " can't be " + std::to_string(bytes.size()) +
                                " bytes long");
  Array out;
  out.dtype_ = dtype;
  out.shape_ = checked_shape(std::move(shape), bytes.size() / dtype_size(dtype));
  out.data_ = std::make_shared<const std::vector<std::uint8_t>>(bytes.begin(), bytes.end());
  return out;
}

std::size_t Array::size() const { return data_ ? data_->size() / dtype_size(dtype_) : 0; }

std::span<const std::uint8_t> Array::bytes() const {
  return data_ ? std::span<const std::uint8_t>(*data_) : std::span<const std::uint8_t>();
}

double Array::at(std::size_t i) const {
  if (i >= size()) throw std::out_of_range("array index " + std::to_string(i) + " out of " + std::to_string(size()));
  const std::size_t width = dtype_size(dtype_);
  std::uint8_t raw[8];
  std::memcpy(raw, data_->data() + i * width, width);
  if constexpr (std::endian::native != std::endian::little) std::reverse(raw, raw + width);
  switch (dtype_) {
    case DType::F32: { float v; std::memcpy(&v, raw, 4); return v; }
    case DType::F64: { double v; std::memcpy(&v, raw, 8); return v; }
    case DType::I32: { std::int32_t v; std::memcpy(&v, raw, 4); return v; }
    case DType::U32: { std::uint32_t v; std::memcpy(&v, raw, 4); return v; }
    case DType::U8: return raw[0];
  }
  return 0;
}

bool operator==(const Array& a, const Array& b) {
  if (a.dtype_ != b.dtype_ || a.shape_ != b.shape_) return a.empty() && b.empty();
  if (a.data_ == b.data_) return true;
  return std::ranges::equal(a.bytes(), b.bytes());
}

}  // namespace paperboat::toolkit
