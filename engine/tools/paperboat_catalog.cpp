// paperboat-catalog: inspect the toolkit and domain descriptors.
//
//   paperboat-catalog --templates          list every toolkit template
//   paperboat-catalog <descriptor.toml>    print the protocol catalog message it produces
//   paperboat-catalog --starters <descriptor.toml>
//                                          print {catalog, graph, station} as JSON

#include "paperboat/toolkit/plugin.hpp"
#include <iostream>
#include <string_view>

#include <nlohmann/json.hpp>

#include "paperboat/toolkit/descriptor.hpp"
#ifdef PAPERBOAT_HAS_OPENCV
#include "paperboat/toolkit/opencv.hpp"
#endif
#ifdef PAPERBOAT_HAS_OPTIMIZATION
#include "paperboat/toolkit/optimization.hpp"
#endif
#ifdef PAPERBOAT_HAS_CONTROL
#include "paperboat/toolkit/control.hpp"
#endif

namespace tk = paperboat::toolkit;

namespace {

// The builtins plus the optional packages this build has.
const tk::Registry& registry() {
  static const tk::Registry r = [] {
    tk::Registry all = tk::builtins();
#ifdef PAPERBOAT_HAS_OPENCV
    tk::opencv::register_opencv(all);
#endif
#ifdef PAPERBOAT_HAS_OPTIMIZATION
    tk::optimization::register_optimization(all);
#endif
#ifdef PAPERBOAT_HAS_CONTROL
    tk::control::register_control(all);
#endif
    tk::load_plugins(all);  // installed toolkits, when they are plugins (pb#352)
    return all;
  }();
  return r;
}

std::string ports(const std::vector<tk::PortDef>& list) {
  std::string out;
  for (const auto& p : list) out += (out.empty() ? "" : ", ") + p.id;
  return out.empty() ? "-" : out;
}

void list_templates() {
  for (const auto* t : registry().all()) {
    std::cout << t->name << "  (" << tk::to_string(t->def.kind) << ")\n"
              << "    in: " << ports(t->def.inputs) << "   out: " << ports(t->def.outputs) << '\n';
    if (!t->def.params.empty()) {
      std::cout << "    params:";
      for (const auto& p : t->def.params) {
        std::cout << ' ' << p.id << '=';
        if (p.type == "number") std::cout << p.default_value;
        else std::cout << '"' << p.default_text << "\" (" << p.type << ')';
      }
      std::cout << '\n';
    }
    if (!t->config.empty()) {
      std::cout << "    config:";
      for (const auto& [k, v] : t->config) std::cout << ' ' << k << "=\"" << v << '"';
      std::cout << '\n';
    }
  }
}

}  // namespace

int main(int argc, char** argv) {
  const bool starters = argc == 3 && std::string_view(argv[1]) == "--starters";
  if (argc != 2 && !starters) {
    std::cerr << "usage: paperboat-catalog --templates | [--starters] <descriptor.toml>\n";
    return 2;
  }
  const std::string_view arg = argv[argc - 1];
  if (arg == "--templates") {
    list_templates();
    return 0;
  }
  try {
    const auto catalog = tk::load_descriptor_file(arg, registry());
    if (starters) {
      nlohmann::json out = {{"catalog", nlohmann::json::parse(tk::catalog_message(catalog))},
                            {"graph", nlohmann::json::parse(catalog.graph)},
                            {"station", catalog.station.empty() ? nlohmann::json() : nlohmann::json::parse(catalog.station)}};
      std::cout << out.dump(2) << '\n';
      return 0;
    }
    std::cout << tk::catalog_message(catalog, 2) << '\n';
  } catch (const tk::DescriptorError& err) {
    std::cerr << arg << ": " << err.what() << '\n';
    return 1;
  }
}
