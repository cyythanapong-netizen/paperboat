// string/text_test.

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <string>
#include <string_view>
#include <vector>

#include "common.hpp"
#include "paperboat/toolkit/bases.hpp"

namespace paperboat::toolkit::string {
namespace {

bool evaluate_text_test(const std::string& text, const std::string& query, int mode) {
  switch (mode) {
    case 0: return text.find(query) != std::string::npos;
    case 1: return text.rfind(query, 0) == 0;
    case 2: return text.size() >= query.size() && text.compare(text.size() - query.size(), query.size(), query) == 0;
    default: return false;
  }
}

class TextTestValue final : public Block {
 public:
  explicit TextTestValue(int mode) : mode_(mode) {}
  Values step(const Context&, const Values& in) override {
    std::string text = in.contains("text") ? value_to_text(in.at("text")) : "";
    std::string test = in.contains("test") ? value_to_text(in.at("test")) : "";
    return {{"y", evaluate_text_test(text, test, mode_) ? 1.0 : 0.0}};
  }

 private:
  int mode_;
};

class TextTestBranch final : public Step {
 public:
  explicit TextTestBranch(int mode) : mode_(mode) {}
  std::string tick(const Context&, const Values& in) override {
    std::string text = in.contains("text") ? value_to_text(in.at("text")) : "";
    std::string test = in.contains("test") ? value_to_text(in.at("test")) : "";
    return evaluate_text_test(text, test, mode_) ? "yes" : "no";
  }

 private:
  int mode_;
};

}  // namespace

void add_text_test(Registry& registry) {
  // string/text_test
  Template tt;
  tt.name = "string/text_test";
  tt.def = magic_def("text_test", "Text Test",
                     {{.name = "signal", .inputs = {{.id = "text", .type = "text"}, {.id = "test", .type = "text"}}, .outputs = {{"y"}}},
                      {.name = "step", .inputs = {{"in", "step"}, {.id = "text", .type = "text"}, {.id = "test", .type = "text"}}, .outputs = {{"yes", "step"}, {"no", "step"}}}},
                     {{.id = "mode", .type = "enum", .default_text = "contains", .options = {"contains", "starts_with", "ends_with"}}});
  tt.def.description = "Tests if text contains, starts with, or ends with a query string.";
  tt.def.view = {.compact = true, .glyph = "T?", .value = "{mode}"};
  tt.make = [](const Values& p, const Config&) {
    int mode = static_cast<int>(get_or(p, "mode", 0.0));
    return std::make_unique<TextTestValue>(mode);
  };
  tt.make_step = [](const Values& p, const Config&) {
    int mode = static_cast<int>(get_or(p, "mode", 0.0));
    return std::make_unique<TextTestBranch>(mode);
  };
  registry.add(std::move(tt));
}

}  // namespace paperboat::toolkit::string
