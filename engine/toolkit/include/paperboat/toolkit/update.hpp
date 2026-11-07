#pragma once

// Updating git checkouts in place: the paperboat repository itself (`paperboat update`) and the
// sibling app checkouts (paperboat-shared, squero-studio-om) next to it.

#include <filesystem>
#include <string>
#include <vector>

#include "paperboat/toolkit/installer.hpp"

namespace paperboat::toolkit {

struct CheckoutStatus {
  std::filesystem::path dir;
  std::string branch;                 // empty when HEAD is detached
  std::string upstream;               // e.g. "origin/dev"
  int ahead = 0;                      // local commits the upstream lacks
  int behind = 0;                     // upstream commits not here yet
  bool dirty = false;                 // tracked files with uncommitted changes
  std::vector<std::string> incoming;  // subjects of the commits behind, newest first (at most 10)
  std::string error;                  // why it can't be updated (not a checkout, no upstream, offline, ...)
};

// `git fetch`, then compare with the branch's upstream.
CheckoutStatus check_checkout(const std::filesystem::path& dir, const ToolkitLog& log = {});

// `git pull --ff-only`; `changed` gets the paths the pull touched. Refused when the tree is dirty
// or the branch has diverged.
ToolkitResult pull_checkout(const std::filesystem::path& dir, std::vector<std::string>* changed = nullptr,
                            const ToolkitLog& log = {});

}  // namespace paperboat::toolkit
