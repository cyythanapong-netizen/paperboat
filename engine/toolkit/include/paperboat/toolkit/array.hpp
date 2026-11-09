#pragma once

// A typed array: bulk numbers that don't belong in JSON (a mesh's vertices and triangles, a
// field over it). Its elements are kept little-endian and row-major, exactly as they go on the
// wire: a client that asked for binary frames gets them as one (protocol/README.md, "Binary
// frames"), any other as a flat JSON array of numbers. Shared and immutable, so a station
// object can hand out the same mesh every frame without copying it.
//
//   paperboat::Array vertices(std::vector<float>{0, 0, 0, 1, 0, 0, 0, 1, 0}, {3, 3});
//   paperboat::Array triangles(std::vector<std::uint32_t>{0, 1, 2}, {1, 3});

#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <span>
#include <string_view>
#include <type_traits>
#include <vector>

namespace paperboat::toolkit {

// An element type, as the wire names it: f32, f64, i32, u32, u8.
enum class DType : std::uint8_t { F32, F64, I32, U32, U8 };

std::string_view dtype_name(DType dtype);
std::size_t dtype_size(DType dtype);
std::optional<DType> dtype_from(std::string_view name);  // null for a name not above

template <class T>
inline constexpr bool is_array_element_v =
    std::is_same_v<T, float> || std::is_same_v<T, double> || std::is_same_v<T, std::int32_t> ||
    std::is_same_v<T, std::uint32_t> || std::is_same_v<T, std::uint8_t>;

template <class T>
  requires is_array_element_v<T>
constexpr DType dtype_of() {
  if constexpr (std::is_same_v<T, float>) return DType::F32;
  else if constexpr (std::is_same_v<T, double>) return DType::F64;
  else if constexpr (std::is_same_v<T, std::int32_t>) return DType::I32;
  else if constexpr (std::is_same_v<T, std::uint32_t>) return DType::U32;
  else return DType::U8;
}

class Array {
 public:
  Array() = default;  // empty: no elements, sent as nothing at all

  // `values` in row-major order; `shape` empty means one dimension of values.size().
  // Throws std::invalid_argument when the shape doesn't hold values.size() elements.
  template <class T>
    requires is_array_element_v<T>
  explicit Array(const std::vector<T>& values, std::vector<std::size_t> shape = {})
      : Array(dtype_of<T>(), std::move(shape), values.data(), values.size()) {}

  // `count` elements of `dtype` at `data`, in this machine's byte order (copied).
  Array(DType dtype, std::vector<std::size_t> shape, const void* data, std::size_t count);

  // Elements already little-endian (as a frame's payload carries them).
  static Array from_le_bytes(DType dtype, std::vector<std::size_t> shape, std::span<const std::uint8_t> bytes);

  DType dtype() const { return dtype_; }
  const std::vector<std::size_t>& shape() const { return shape_; }
  std::size_t size() const;  // elements: the product of the shape
  bool empty() const { return size() == 0; }
  std::span<const std::uint8_t> bytes() const;  // little-endian, row-major
  double at(std::size_t i) const;              // element i (row-major) as a number

  // Same dtype, shape and bytes. Cheap when both share their data (the same mesh again).
  friend bool operator==(const Array& a, const Array& b);

 private:
  DType dtype_ = DType::F64;
  std::vector<std::size_t> shape_ = {};
  std::shared_ptr<const std::vector<std::uint8_t>> data_ = {};
};

}  // namespace paperboat::toolkit
