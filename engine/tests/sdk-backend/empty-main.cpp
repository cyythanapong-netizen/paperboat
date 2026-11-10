// The web app's empty-Flow fixture (apps/web/e2e): a backend whose descriptor has no
// starter [graph], so the Flow page opens with only the block its station object spawns —
// or with nothing at all when PAPERBOAT_EMPTY_STATION=1. Built in this project so the e2e
// suite reuses the sdk-backend's engine build.
#include <cstdlib>
#include <vector>

#include <paperboat/sdk.hpp>

namespace {

struct Thermo final : paperboat::StationObject {
  explicit Thermo(const paperboat::Values& props) : celsius(paperboat::get_or(props, "celsius")) {}
  paperboat::Values read() const override { return {{"celsius", celsius}}; }
  void step(const paperboat::Context&, const paperboat::Values&) override {}
  std::vector<paperboat::Entity> entities(const std::string&, double) const override { return {}; }
  double celsius;
};

}  // namespace

int main(int argc, char** argv) {
  static const paperboat::Registry registry = [] {
    paperboat::Registry r = paperboat::builtins();
    r.add(paperboat::define_object<Thermo>(
        "sdk-backend-empty",
        {.type = "thermo",
         .label = "Thermometer",
         .role = "body",
         .props = {{.id = "celsius", .default_value = 20}},
         .links = {},
         .block = paperboat::BlockDef{.type = "thermo",
                                      .label = "Thermometer",
                                      .kind = paperboat::Kind::Probe,
                                      .outputs = {{"celsius"}}},
         .description = "A reading on the wall."}));
    return r;
  }();
  static const paperboat::Catalog with_station = paperboat::load_descriptor_file(PAPERBOAT_STATION_DESCRIPTOR, registry);
  static const paperboat::Catalog empty = paperboat::load_descriptor_file(PAPERBOAT_EMPTY_DESCRIPTOR, registry);
  const char* empty_env = std::getenv("PAPERBOAT_EMPTY_STATION");
  const bool nothing = empty_env && *empty_env;
  return paperboat::main({.engine = "cpp-sdk-backend-empty", .catalog = nothing ? &empty : &with_station}, argc, argv, 8783);
}
