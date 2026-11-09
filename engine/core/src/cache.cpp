#include "paperboat/core/cache.hpp"

#include <algorithm>
#include <bit>
#include <cstdint>
#include <type_traits>
#include <variant>

namespace paperboat::core {

namespace {

bool same_number(double a, double b) { return std::bit_cast<std::uint64_t>(a) == std::bit_cast<std::uint64_t>(b); }

bool same_numbers(const toolkit::Vector& a, const toolkit::Vector& b) {
  return std::ranges::equal(a, b, same_number);
}

}  // namespace

bool same_value(const toolkit::Value& a, const toolkit::Value& b) {
  if (a.index() != b.index()) return false;
  return std::visit(
      [&](const auto& x) {
        using T = std::decay_t<decltype(x)>;
        const auto& y = std::get<T>(b);
        if constexpr (std::is_same_v<T, double>) return same_number(x, y);
        else if constexpr (std::is_same_v<T, toolkit::Vector>) return same_numbers(x, y);
        else if constexpr (std::is_same_v<T, toolkit::Table>) return std::ranges::equal(x, y, same_numbers);
        else if constexpr (std::is_same_v<T, std::string>) return x == y;
        else return x.kind == y.kind && x.data == y.data;  // a handle: the same object
      },
      static_cast<const toolkit::Value::variant&>(a));
}

bool same_values(const toolkit::Values& a, const toolkit::Values& b) {
  return std::ranges::equal(a, b, [](const auto& x, const auto& y) { return x.first == y.first && same_value(x.second, y.second); });
}

std::shared_ptr<const RunCache::Entry> RunCache::find(std::string_view node, std::string_view signature,
                                                      const toolkit::Values& inputs, const toolkit::Values& variables) const {
  std::shared_ptr<const Entry> entry;
  {
    std::lock_guard lock(mutex_);
    const auto it = entries_.find(node);
    if (it == entries_.end()) return nullptr;
    entry = it->second;
  }
  if (entry->signature != signature || !same_values(entry->inputs, inputs)) return nullptr;
  for (const auto& [name, was] : entry->variables) {
    const auto now = variables.find(name);
    if (now == variables.end() ? was.has_value() : !was || !same_value(*was, now->second)) return nullptr;
  }
  return entry;
}

void RunCache::store(const std::string& node, Entry entry) {
  auto shared = std::make_shared<const Entry>(std::move(entry));
  std::lock_guard lock(mutex_);
  entries_.insert_or_assign(node, std::move(shared));
}

void RunCache::erase(std::string_view node) {
  std::lock_guard lock(mutex_);
  if (const auto it = entries_.find(node); it != entries_.end()) entries_.erase(it);
}

void RunCache::keep_only(const std::set<std::string, std::less<>>& nodes) {
  std::lock_guard lock(mutex_);
  std::erase_if(entries_, [&](const auto& item) { return !nodes.contains(item.first); });
}

void RunCache::clear() {
  std::lock_guard lock(mutex_);
  entries_.clear();
}

std::size_t RunCache::size() const {
  std::lock_guard lock(mutex_);
  return entries_.size();
}

bool RunCache::contains(std::string_view node) const {
  std::lock_guard lock(mutex_);
  return entries_.find(node) != entries_.end();
}

}  // namespace paperboat::core
