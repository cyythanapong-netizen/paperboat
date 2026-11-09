// Binary frames (#18): typed arrays, the frame layout, a message's arrays as frames or JSON
// numbers, a session that sends them, and a run that sends a mesh again only when it changes.
// protocol/vectors/binary-frames-v1.json is this encoder's output: the web decoder's tests
// read it. PAPERBOAT_WRITE_VECTORS=1 rewrites it after a deliberate change to the layout.

#include <cstdint>
#include <cstdlib>
#include <fstream>
#include <mutex>
#include <sstream>
#include <string>
#include <vector>

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_string.hpp>

#include "paperboat/core/backend.hpp"
#include "paperboat/core/binary.hpp"
#include "paperboat/core/station.hpp"
#include "paperboat/toolkit/scene.hpp"
#include "rig.hpp"

namespace core = paperboat::core;
namespace tk = paperboat::toolkit;
using core::json;
using Catch::Matchers::ContainsSubstring;

namespace {

std::string hex(const std::string& bytes) {
  static const char* digits = "0123456789abcdef";
  std::string out;
  for (char ch : bytes) {
    const auto c = static_cast<unsigned char>(ch);
    out += {digits[c >> 4], digits[c & 15]};
  }
  return out;
}

// A plate of two triangles with a field over its four corners.
tk::Entity plate(float corner = 1.0f) {
  return tk::scene::mesh("plate", tk::Array(std::vector<float>{0, 0, 0, 1, 0, 0, 0, 1, 0, 1, 1, 0.1f}, {4, 3}),
                         tk::Array(std::vector<std::uint32_t>{0, 1, 2, 2, 1, 3}, {2, 3}), "#2f5d62", {0, 0.5, 0},
                         tk::Array(std::vector<float>{0, 0.25f, 0.5f, corner}), {0, 1});
}

// One array per dtype, the corner cases of each.
std::vector<std::pair<std::string, tk::Array>> cases() {
  return {{"f32", tk::Array(std::vector<float>{0.1f, -2.5f, 1024, 1e-7f}, {2, 2})},
          {"f64", tk::Array(std::vector<double>{3.141592653589793, -1e300, 0.5})},
          {"i32", tk::Array(std::vector<std::int32_t>{-1, 0, 2147483647, -2147483647 - 1})},
          {"u32", tk::Array(std::vector<std::uint32_t>{0, 1, 4294967295u})},
          {"u8", tk::Array(std::vector<std::uint8_t>{0, 7, 255, 128, 1})},
          {"empty", tk::Array(std::vector<float>{}, {0, 3})}};
}

json numbers_of(const tk::Array& a) { return core::lower_arrays(core::array_json(a)); }

// The spec vector, as this encoder makes it.
json vectors() {
  json frames = json::array();
  std::uint64_t id = 1;
  for (const auto& [name, array] : cases()) {
    const json descriptor = {{"id", std::to_string(id++)}, {"dtype", tk::dtype_name(array.dtype())}, {"shape", array.shape()}};
    frames.push_back({{"name", name},
                      {"descriptor", descriptor},
                      {"values", numbers_of(array)},
                      {"frame", hex(core::encode_frame(descriptor, array.bytes()))}});
  }
  const json scene = {{"type", "scene"}, {"entities", {core::entity_json(plate())}}};
  std::vector<std::string> sent;
  std::uint64_t next = 1;
  const json lowered = core::lower_arrays(scene, &sent, &next);
  json hexed = json::array();
  for (const auto& f : sent) hexed.push_back(hex(f));
  return {{"about", "Binary frames, layout version 1 (protocol/README.md). Written by engine/tests/binary_test.cpp from the "
                    "C++ encoder; the web decoder's tests read it. frames: one array per dtype (frame is hex). message: a scene "
                    "with a mesh as a JSON-only client gets it (json), and as a client with binary frames does (frames, in order, "
                    "then text)."},
          {"version", core::kFrameVersion},
          {"frames", frames},
          {"message", {{"json", core::lower_arrays(scene)}, {"text", lowered}, {"frames", hexed}}}};
}

}  // namespace

TEST_CASE("an array keeps its dtype, shape and little-endian bytes") {
  const tk::Array a(std::vector<std::int32_t>{1, -2, 3, 4, 5, 6}, {2, 3});
  CHECK(a.dtype() == tk::DType::I32);
  CHECK(a.shape() == std::vector<std::size_t>{2, 3});
  CHECK(a.size() == 6);
  CHECK(a.at(1) == -2);
  CHECK(a.bytes().size() == 24);
  CHECK(a.bytes()[4] == 0xfe);  // -2, least significant byte first
  CHECK(a.bytes()[7] == 0xff);
  CHECK(a == tk::Array::from_le_bytes(tk::DType::I32, {2, 3}, a.bytes()));
  CHECK_FALSE(a == tk::Array(std::vector<std::int32_t>{1, -2, 3, 4, 5, 6}, {3, 2}));
  CHECK(tk::Array().empty());
  CHECK(tk::Array() == tk::Array(std::vector<float>{}));
  CHECK_THROWS_WITH(tk::Array(std::vector<float>{1, 2, 3}, {2, 2}), ContainsSubstring("holds 4 elements, not 3"));
  CHECK_THROWS(tk::Array::from_le_bytes(tk::DType::F64, {}, std::vector<std::uint8_t>(12)));
  CHECK(tk::dtype_from("u32") == tk::DType::U32);
  CHECK_FALSE(tk::dtype_from("f16"));
}

TEST_CASE("a frame is magic, version, descriptor, padding to 8 and the payload") {
  for (const auto& [name, array] : cases()) {
    INFO(name);
    const json descriptor = {{"id", "7"}, {"dtype", tk::dtype_name(array.dtype())}, {"shape", array.shape()}};
    const std::string frame = core::encode_frame(descriptor, array.bytes());
    CHECK(frame.substr(0, 4) == "PBIN");
    CHECK(frame[4] == 1);
    const std::size_t length = descriptor.dump().size();
    CHECK(static_cast<unsigned char>(frame[8]) == length);
    const std::size_t start = (12 + length + 7) / 8 * 8;
    CHECK(start % 8 == 0);
    CHECK(frame.size() == start + array.bytes().size());
    const auto back = core::decode_frame(frame);
    CHECK(back.descriptor == descriptor);
    CHECK(back.array == array);
  }
  // Whatever the descriptor's length, the payload starts aligned (a Float64Array over it, no copy).
  for (std::string id; id.size() < 9; id += "x") {
    const auto frame = core::encode_frame({{"id", id}, {"dtype", "f64"}, {"shape", {1}}}, tk::Array(std::vector<double>{2.5}).bytes());
    CHECK(frame.size() % 8 == 0);
    CHECK(core::decode_frame(frame).array.at(0) == 2.5);
  }
}

TEST_CASE("a decoder refuses what isn't a version-1 frame") {
  const auto good = core::encode_frame({{"id", "1"}, {"dtype", "u8"}, {"shape", {2}}}, std::vector<std::uint8_t>{1, 2});
  auto with = [&](std::size_t at, char c) {
    auto bad = good;
    bad[at] = c;
    return bad;
  };
  CHECK_THROWS_WITH(core::decode_frame(with(0, 'X')), ContainsSubstring("not a Paperboat binary frame"));
  CHECK_THROWS_WITH(core::decode_frame(with(4, 2)), ContainsSubstring("version 2"));
  CHECK_THROWS_WITH(core::decode_frame(with(5, 1)), ContainsSubstring("flags"));
  CHECK_THROWS_WITH(core::decode_frame(with(8, 100)), ContainsSubstring("runs past"));
  CHECK_THROWS(core::decode_frame(good.substr(0, good.size() - 1)));  // payload one short of its shape
  CHECK_THROWS_WITH(core::decode_frame(core::encode_frame({{"id", "1"}, {"dtype", "f16"}, {"shape", {1}}}, {})),
                    ContainsSubstring("dtype f16"));
  CHECK_THROWS_WITH(core::decode_frame(core::encode_frame({{"id", "1"}, {"dtype", "u8"}, {"shape", {0}}, {"compression", "zstd"}}, {})),
                    ContainsSubstring("compression zstd"));
  CHECK_THROWS(core::decode_frame("PBI"));
}

TEST_CASE("a message's arrays become JSON numbers, or references to frames sent before it") {
  const json scene = {{"type", "scene"}, {"entities", {core::entity_json(plate())}}};
  CHECK(core::has_arrays(scene));
  CHECK_FALSE(core::has_arrays(json{{"type", "log"}, {"text", "hi"}}));

  // A JSON-only client: flat arrays, an f32 as its shortest decimal.
  const json plain = core::lower_arrays(scene);
  CHECK_FALSE(core::has_arrays(plain));
  const auto& e = plain["entities"][0];
  CHECK(e["shape"] == "mesh");
  CHECK(e["vertices"] == json{0, 0, 0, 1, 0, 0, 0, 1, 0, 1, 1, 0.1});
  CHECK(e["indices"] == json{0, 1, 2, 2, 1, 3});
  CHECK(e["values"] == json{0, 0.25, 0.5, 1});
  CHECK(e["range"] == json{0, 1});
  CHECK(plain.dump().find("0.1]") != std::string::npos);  // not 0.10000000149011612

  // A client with binary frames: one per array, in the message's order, ids counting up.
  std::vector<std::string> frames;
  std::uint64_t next = 5;
  const json lowered = core::lower_arrays(scene, &frames, &next);
  REQUIRE(frames.size() == 3);
  CHECK(next == 8);
  const auto& r = lowered["entities"][0];
  CHECK(r["indices"] == json{{"binary", "5"}});  // keys in order: indices, values, vertices
  CHECK(r["values"] == json{{"binary", "6"}});
  CHECK(r["vertices"] == json{{"binary", "7"}});
  const auto vertices = core::decode_frame(frames[2]);
  CHECK(vertices.descriptor["id"] == "7");
  CHECK(vertices.descriptor["path"] == "/entities/0/vertices");
  CHECK(vertices.descriptor["shape"] == json{4, 3});
  CHECK(vertices.array == plate().vertices);
  CHECK(core::decode_frame(frames[0]).descriptor["dtype"] == "u32");

  CHECK_THROWS_WITH(tk::scene::mesh("m", tk::Array(std::vector<float>{1, 2}), {}), ContainsSubstring("3 numbers per vertex"));
}

TEST_CASE("the spec vector is this encoder's output") {
  const std::string path = std::string(PAPERBOAT_PROTOCOL_DIR) + "/vectors/binary-frames-v1.json";
  const json now = vectors();
  if (std::getenv("PAPERBOAT_WRITE_VECTORS")) {
    std::ofstream(path) << now.dump(2) << '\n';
    WARN("wrote " << path);
  }
  std::ifstream in(path);
  REQUIRE(in);
  CHECK(json::parse(in) == now);
}

TEST_CASE("a client asks for binary frames in its WebSocket URL") {
  CHECK(core::wants_binary("/?binary=1"));
  CHECK(core::wants_binary("/?x=2&binary=3"));  // reads a later version: gets version 1
  CHECK_FALSE(core::wants_binary("/"));
  CHECK_FALSE(core::wants_binary("/?binary=0"));
  CHECK_FALSE(core::wants_binary("/?binary=yes"));
  CHECK_FALSE(core::wants_binary("/?notbinary=1"));
}

namespace {

// A domain whose state is a plate with a field that changes every step: the mesh stays.
struct Field final : tk::DomainState {
  tk::Entity mesh = plate();
  int steps = 0;
  std::vector<tk::Entity> entities() const override { return {mesh}; }
  std::optional<std::string> step(const tk::Context&) override {
    ++steps;
    if (steps % 2 == 0) mesh.values = tk::Array(std::vector<float>{0, 0.25f, 0.5f, static_cast<float>(steps)});
    return std::nullopt;
  }
};

const core::Backend& field_backend() {
  static const core::Backend backend{.engine = "field",
                                     .catalog = &rig::catalog(),
                                     .state = [] { return std::make_unique<Field>(); },
                                     .run = {.dt = 0.01, .duration = 0.2, .emit_every = 1, .real_time = false}};
  return backend;
}

}  // namespace

TEST_CASE("a session sends a mesh as binary frames to a client that asked, as numbers to one that didn't") {
  struct Wire {
    std::mutex mutex;
    std::vector<std::string> sent;  // "T" + text or "B" + bytes, in order
  };
  auto connect = [](Wire& wire, bool binary) {
    core::Session::SendBinary to_binary;
    if (binary) to_binary = [&wire](const std::string& b) { std::lock_guard l(wire.mutex); wire.sent.push_back("B" + b); };
    return std::make_unique<core::Session>(
        field_backend(), [&wire](const std::string& t) { std::lock_guard l(wire.mutex); wire.sent.push_back("T" + t); },
        std::move(to_binary));
  };

  auto plate_of = [](const json& scene) {
    for (const auto& e : scene["entities"])
      if (e["id"] == "plate") return e;
    return json();
  };
  Wire plain;
  connect(plain, false);
  CHECK_FALSE(json::parse(plain.sent[0].substr(1)).contains("binary"));
  json scene;
  for (const auto& m : plain.sent) {
    CHECK(m[0] == 'T');
    if (const auto msg = json::parse(m.substr(1)); msg["type"] == "scene") scene = msg;
  }
  CHECK(plate_of(scene)["indices"] == json{0, 1, 2, 2, 1, 3});

  Wire wire;
  connect(wire, true);
  CHECK(json::parse(wire.sent[0].substr(1))["binary"] == 1);
  // The scene's three frames go right before it, and nothing else binary.
  std::size_t first = 0;
  while (first < wire.sent.size() && wire.sent[first][0] != 'B') ++first;
  REQUIRE(first + 3 < wire.sent.size());
  for (std::size_t i = first; i < first + 3; ++i) CHECK(wire.sent[i][0] == 'B');
  const json refs = json::parse(wire.sent[first + 3].substr(1));
  CHECK(refs["type"] == "scene");
  CHECK(plate_of(refs)["vertices"] == json{{"binary", "3"}});
  CHECK(core::decode_frame(wire.sent[first + 2].substr(1)).array == plate().vertices);
  for (std::size_t i = first + 3; i < wire.sent.size(); ++i) CHECK(wire.sent[i][0] == 'T');
}

TEST_CASE("a run sends a mesh in its scene, then only the arrays that change") {
  std::vector<json> out;
  const auto graph = json::parse(rig::catalog().graph);
  const auto station = json::parse(rig::catalog().station);
  REQUIRE(core::run("r", graph, station, field_backend(), [&](const json& m) { out.push_back(m); }, nullptr, {}) == "done");
  int scenes = 0, patches = 0, with_values = 0, with_vertices = 0;
  for (const auto& m : out) {
    if (m["type"] == "scene") {
      ++scenes;
      CHECK(core::is_array_json(m["entities"].back()["vertices"]));
    }
    if (m["type"] != "scene.patch") continue;
    for (const auto& e : m["entities"]) {
      if (e["id"] != "plate") continue;
      ++patches;
      with_values += e.contains("values");
      with_vertices += e.contains("vertices") || e.contains("indices");
      CHECK(e["pos"] == json{0, 0.5, 0});  // the rest of the entity still goes, as for any other
    }
  }
  CHECK(scenes == 1);
  CHECK(patches > 10);
  CHECK(with_vertices == 0);                 // the same mesh: never again
  CHECK(with_values > 0);
  CHECK(with_values <= patches / 2 + 1);     // the field: only when it changed (every other step)
}
