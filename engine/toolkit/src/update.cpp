#include "paperboat/toolkit/update.hpp"

#include <cctype>
#include <sstream>

namespace paperboat::toolkit {
namespace {

namespace fs = std::filesystem;

std::string trimmed(std::string s) {
  while (!s.empty() && std::isspace(static_cast<unsigned char>(s.back()))) s.pop_back();
  return s;
}

// Runs `git -C dir <args>`; out gets its output. False when it fails.
bool git(const fs::path& dir, std::vector<std::string> args, std::string& out, const ToolkitLog& log = {}) {
  args.insert(args.begin(), {"git", "-C", dir.string()});
  out.clear();
  return run_command(args, {}, log, nullptr, &out) == 0;
}

}  // namespace

CheckoutStatus check_checkout(const fs::path& dir, const ToolkitLog& log) {
  CheckoutStatus st;
  st.dir = dir;
  std::string out;
  if (!fs::exists(dir / ".git") || !git(dir, {"rev-parse", "--is-inside-work-tree"}, out)) {
    st.error = "not a git checkout";
    return st;
  }
  if (git(dir, {"symbolic-ref", "--short", "-q", "HEAD"}, out)) st.branch = trimmed(out);
  if (st.branch.empty()) {
    st.error = "HEAD is detached";
    return st;
  }
  if (!git(dir, {"rev-parse", "--abbrev-ref", "@{upstream}"}, out)) {
    st.error = "branch " + st.branch + " has no upstream (git branch --set-upstream-to=origin/" + st.branch + ")";
    return st;
  }
  st.upstream = trimmed(out);
  if (!git(dir, {"fetch", "--quiet"}, out, log)) {
    st.error = "cannot fetch (offline?)";
    return st;
  }
  if (git(dir, {"rev-list", "--left-right", "--count", "HEAD..." + st.upstream}, out)) {
    std::istringstream in(out);
    in >> st.ahead >> st.behind;
  }
  if (git(dir, {"status", "--porcelain", "--untracked-files=no"}, out)) st.dirty = !trimmed(out).empty();
  if (st.behind > 0 && git(dir, {"log", "--format=%s", "-10", "HEAD.." + st.upstream}, out)) {
    std::istringstream in(out);
    for (std::string line; std::getline(in, line);) st.incoming.push_back(line);
  }
  return st;
}

ToolkitResult pull_checkout(const fs::path& dir, std::vector<std::string>* changed, const ToolkitLog& log) {
  const auto st = check_checkout(dir, log);
  if (!st.error.empty()) return {false, st.error};
  if (st.behind == 0) return {true, "up to date"};
  if (st.dirty) return {false, "uncommitted changes in tracked files; commit or stash them first"};
  if (st.ahead > 0)
    return {false, "diverged from " + st.upstream + " (" + std::to_string(st.ahead) + " ahead, " +
                       std::to_string(st.behind) + " behind); merge or rebase by hand"};
  std::string before, after, out;
  git(dir, {"rev-parse", "HEAD"}, before);
  if (!git(dir, {"pull", "--ff-only", "--quiet"}, out, log)) return {false, "git pull failed: " + trimmed(out)};
  git(dir, {"rev-parse", "HEAD"}, after);
  if (changed && git(dir, {"diff", "--name-only", trimmed(before), trimmed(after)}, out)) {
    std::istringstream in(out);
    for (std::string line; std::getline(in, line);) changed->push_back(line);
  }
  return {true, "pulled " + std::to_string(st.behind) + " commit(s)"};
}

}  // namespace paperboat::toolkit
