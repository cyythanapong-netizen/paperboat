// Command-line toolkit manager for optional external Paperboat toolkits:
//   paperboat-toolkit list [--json]
//   paperboat-toolkit install <name> [--no-build]
//   paperboat-toolkit update <name> [--no-build]
//   paperboat-toolkit uninstall <name> [--no-build]

#include <iostream>
#include <string>
#include <string_view>
#include <vector>

#include "paperboat/toolkit/descriptor.hpp"
#include "paperboat/toolkit/installer.hpp"
#ifdef PAPERBOAT_HAS_OPENCV
#include "paperboat/toolkit/opencv.hpp"
#endif
#ifdef PAPERBOAT_HAS_OPTIMIZATION
#include "paperboat/toolkit/optimization.hpp"
#endif

namespace {

const paperboat::toolkit::Registry& full_registry() {
  static const paperboat::toolkit::Registry r = [] {
    paperboat::toolkit::Registry all = paperboat::toolkit::builtins();
#ifdef PAPERBOAT_HAS_OPENCV
    paperboat::toolkit::opencv::register_opencv(all);
#endif
#ifdef PAPERBOAT_HAS_OPTIMIZATION
    paperboat::toolkit::optimization::register_optimization(all);
#endif
    return all;
  }();
  return r;
}

}  // namespace

int main(int argc, char** argv) {
  namespace tk = paperboat::toolkit;
  std::vector<std::string_view> args(argv + 1, argv + argc);
  bool json_out = false;
  bool rebuild = true;
  std::vector<std::string_view> pos;
  for (std::string_view a : args) {
    if (a == "--json") json_out = true;
    else if (a == "--no-build") rebuild = false;
    else pos.push_back(a);
  }

  const std::string_view cmd = pos.empty() ? "list" : pos[0];
  try {
    if (cmd == "list") {
      if (pos.size() >= 2) {
        const auto res = tk::inspect_toolkit(pos[1], full_registry());
        if (!res.found) {
          std::cerr << "paperboat-toolkit: unknown toolkit or package \"" << pos[1] << "\"\n";
          return 1;
        }
        if (json_out) {
          std::cout << tk::toolkit_inspect_json(res) << '\n';
        } else {
          std::cout << tk::toolkit_inspect_text(res);
        }
        return 0;
      }
      const auto list = tk::list_toolkits();
      if (json_out) {
        std::cout << tk::toolkits_json(list) << '\n';
        return 0;
      }
      for (const auto& t : list) {
        const char* status = t.builtin ? "built-in toolkit" : (t.installed ? "installed" : "not installed");
        std::cout << t.name << "\t" << status << "\t" << (t.repo.empty() ? "-" : t.repo);
        if (!t.description.empty()) std::cout << "\t" << t.description;
        std::cout << '\n';
      }
      return 0;
    }

    if (pos.size() < 2) {
      std::cerr << "usage: paperboat-toolkit <list|install|uninstall> [<name>] [--repo <url>|--path <dir>] [--version <ver>] [--no-build] [--json]\n";
      return 2;
    }
    const std::string_view name = pos[1];
    std::string version;
    std::string source;  // --repo <url> or --path <dir>
    for (std::size_t i = 0; i < args.size(); ++i) {
      if ((args[i] == "--version" || args[i] == "-v") && i + 1 < args.size()) {
        version = std::string(args[i + 1]);
      } else if ((args[i] == "--repo" || args[i] == "--path") && i + 1 < args.size()) {
        source = std::string(args[i + 1]);
      }
    }
    auto log = [](std::string_view level, std::string_view text) {
      (level == "error" ? std::cerr : std::cout) << "[" << level << "] " << text << '\n';
    };

    tk::ToolkitResult res;
    tk::HostBuild host{.source_dir = {}, .build_dir = {}, .skip_rebuild = !rebuild};
    if (cmd == "install" || cmd == "add" || cmd == "update") {
      res = tk::install_toolkit(name, version, {}, host, log, source);
    } else if (cmd == "uninstall" || cmd == "remove") {
      res = tk::uninstall_toolkit(name, {}, host, log);
    } else {
      std::cerr << "paperboat-toolkit: unknown command \"" << cmd << "\"\n";
      return 2;
    }
    if (!res.ok) {
      std::cerr << "paperboat-toolkit: " << res.message << '\n';
      return 1;
    }
    return 0;
  } catch (const tk::DescriptorError& err) {
    std::cerr << "paperboat-toolkit: " << err.what() << '\n';
    return 1;
  }
}
