#include "paperboat/toolkit/registry.hpp"

#include <algorithm>
#include <stdexcept>

namespace paperboat::toolkit {

void register_manifest_packages(Registry& registry);

void Registry::add(Template t) {
  if (find(t.name)) throw std::invalid_argument("duplicate template: " + t.name);
  templates_.push_back(std::move(t));
}

void Registry::add(StationTemplate t) {
  if (find_object(t.name)) throw std::invalid_argument("duplicate station template: " + t.name);
  objects_.push_back(std::move(t));
}

void Registry::add_connector(std::string package, std::string name, nlohmann::json def) {
  if (!def.is_object()) throw std::invalid_argument("connector \"" + name + "\" must be a JSON object");
  for (auto& c : connectors_) {
    if (c.package == package && c.name == name) {
      c.def = std::move(def);
      return;
    }
  }
  connectors_.push_back({.package = std::move(package), .name = std::move(name), .def = std::move(def)});
}

void Registry::add(AnalysisTemplate t) {
  if (find_analysis(t.name)) throw std::invalid_argument("duplicate analysis: " + t.name);
  if (t.name.find('/') == std::string::npos) throw std::invalid_argument("analysis \"" + t.name + "\": name it <package>/<analysis>");
  analyses_.push_back(std::move(t));
}

const AnalysisTemplate* Registry::find_analysis(std::string_view name) const {
  auto it = std::ranges::find(analyses_, name, &AnalysisTemplate::name);
  return it == analyses_.end() ? nullptr : &*it;
}

const StationTemplate* Registry::find_object(std::string_view name) const {
  auto it = std::ranges::find(objects_, name, &StationTemplate::name);
  return it == objects_.end() ? nullptr : &*it;
}

namespace {
bool in_package(const std::string& name, std::string_view package) {
  return name.size() > package.size() && name.starts_with(package) && name[package.size()] == '/';
}
}  // namespace

std::vector<const StationTemplate*> Registry::objects(std::string_view package) const {
  std::vector<const StationTemplate*> out;
  for (const auto& t : objects_) {
    if (in_package(t.name, package)) out.push_back(&t);
  }
  return out;
}

std::vector<const AnalysisTemplate*> Registry::analyses(std::string_view package) const {
  std::vector<const AnalysisTemplate*> out;
  for (const auto& a : analyses_) {
    if (in_package(a.name, package)) out.push_back(&a);
  }
  return out;
}

std::vector<const ConnectorEntry*> Registry::connectors(std::string_view package) const {
  std::vector<const ConnectorEntry*> out;
  for (const auto& c : connectors_) {
    if (c.package == package) out.push_back(&c);
  }
  return out;
}

const Template* Registry::find(std::string_view name) const {
  auto it = std::ranges::find(templates_, name, &Template::name);
  return it == templates_.end() ? nullptr : &*it;
}

std::vector<const Template*> Registry::all() const {
  std::vector<const Template*> out;
  for (const auto& t : templates_) out.push_back(&t);
  std::ranges::sort(out, {}, &Template::name);
  return out;
}

std::vector<const Template*> Registry::package(std::string_view name) const {
  std::vector<const Template*> out;
  for (const auto& t : templates_) {
    if (in_package(t.name, name)) out.push_back(&t);
  }
  return out;
}

std::vector<std::string> Registry::packages() const {
  std::vector<std::string> out;
  auto note = [&](const std::string& full) {
    auto name = full.substr(0, full.find('/'));
    if (std::ranges::find(out, name) == out.end()) out.push_back(name);
  };
  for (const auto& t : templates_) note(t.name);
  for (const auto& t : objects_) note(t.name);
  for (const auto& c : connectors_) {
    if (std::ranges::find(out, c.package) == out.end()) out.push_back(c.package);
  }
  for (const auto& a : analyses_) note(a.name);
  std::ranges::sort(out);
  return out;
}

void Registry::default_flows(std::string_view package, const std::vector<std::string>& flows) {
  check_flows(flows, std::string(package) + "/toolkit.toml");
  for (auto& t : templates_)
    if (in_package(t.name, package) && t.def.flows.empty()) t.def.flows = flows;
}

const Registry& builtins() {
  static const Registry registry = [] {
    Registry r;
    register_manifest_packages(r);
    return r;
  }();
  return registry;
}

}  // namespace paperboat::toolkit
