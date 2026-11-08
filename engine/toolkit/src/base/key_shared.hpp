#pragma once

// Shared by several base blocks.

#include <string>
#include <utility>

#include "common.hpp"

namespace paperboat::toolkit::base {

class Key : public Block {
 public:
  explicit Key(std::string key) : key_(std::move(key)) {}
  Values step(const Context& ctx, const Values&) override {
    bool down = ctx.keys_down != nullptr && ctx.keys_down->contains(key_);
    return {{"pressed", down ? 1.0 : 0.0}};
  }

 private:
  std::string key_;
};

}  // namespace paperboat::toolkit::base
