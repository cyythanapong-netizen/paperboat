// The string package: string manipulation and inspection blocks.

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

class Concat final : public Block {
 public:
  Concat(std::string separator, int count) : separator_(std::move(separator)), count_(count) {}

  Values step(const Context&, const Values& in) override {
    std::string out;
    for (int i = 0; i < count_; ++i) {
      std::string id(1, static_cast<char>('a' + i));
      auto it = in.find(id);
      if (it != in.end()) {
        if (i > 0 && !separator_.empty()) out += separator_;
        out += value_to_text(it->second);
      }
    }
    return {{"y", out}};
  }

 private:
  std::string separator_;
  int count_;
};

class Format final : public Block {
 public:
  explicit Format(std::string tmpl) : template_(std::move(tmpl)) {}

  Values step(const Context&, const Values& in) override {
    std::string out;
    out.reserve(template_.size() + 16);
    for (std::size_t i = 0; i < template_.size(); ++i) {
      if (template_[i] == '{') {
        if (i + 1 < template_.size() && template_[i + 1] == '{') {
          out.push_back('{');
          ++i;
          continue;
        }
        auto end = template_.find('}', i + 1);
        if (end == std::string::npos) {
          out.push_back('{');
          continue;
        }
        auto inner = template_.find('{', i + 1);
        if (inner != std::string::npos && inner < end) {
          out.push_back('{');
          continue;
        }
        std::string_view token = std::string_view(template_).substr(i + 1, end - (i + 1));
        while (!token.empty() && std::isspace(static_cast<unsigned char>(token.front()))) token.remove_prefix(1);
        while (!token.empty() && std::isspace(static_cast<unsigned char>(token.back()))) token.remove_suffix(1);
        std::string key(token);
        auto it = in.find(key);
        if (it != in.end()) {
          out += value_to_text(it->second);
        }
        i = end;
      } else if (template_[i] == '}') {
        if (i + 1 < template_.size() && template_[i + 1] == '}') {
          out.push_back('}');
          ++i;
          continue;
        }
        out.push_back('}');
      } else {
        out.push_back(template_[i]);
      }
    }
    return {{"y", out}};
  }

 private:
  std::string template_;
};

class Split final : public Block {
 public:
  Split(std::string delim, double index) : default_delimiter_(std::move(delim)), default_index_(index) {}

  Values step(const Context&, const Values& in) override {
    std::string text = in.contains("text") ? value_to_text(in.at("text")) : "";
    std::string delim = in.contains("delimiter") ? value_to_text(in.at("delimiter")) : default_delimiter_;
    double index = in.contains("index") ? get_or(in, "index") : default_index_;

    if (text.empty()) {
      return {{"y", std::string("")}, {"vector", Vector{}}, {"count", 0.0}};
    }

    std::vector<std::string> parts;
    if (delim.empty()) {
      for (char c : text) parts.push_back(std::string(1, c));
    } else {
      std::size_t start = 0;
      while (start <= text.size()) {
        std::size_t pos = text.find(delim, start);
        if (pos == std::string::npos) {
          parts.push_back(text.substr(start));
          break;
        }
        parts.push_back(text.substr(start, pos - start));
        start = pos + delim.size();
      }
    }

    Vector vec;
    for (const auto& part : parts) {
      char* end = nullptr;
      double val = std::strtod(part.c_str(), &end);
      if (end != part.c_str() && *end == '\0') {
        vec.push_back(val);
      }
    }

    std::string out_y;
    if (index >= 0) {
      std::size_t idx = static_cast<std::size_t>(index);
      if (idx < parts.size()) out_y = parts[idx];
    } else {
      for (std::size_t i = 0; i < parts.size(); ++i) {
        if (i > 0) out_y += "\n";
        out_y += parts[i];
      }
    }

    return {{"y", out_y}, {"vector", vec}, {"count", static_cast<double>(parts.size())}};
  }

 private:
  std::string default_delimiter_;
  double default_index_;
};

class Replace final : public Block {
 public:
  Replace(std::string find, std::string rep, bool all)
      : default_find_(std::move(find)), default_rep_(std::move(rep)), all_(all) {}

  Values step(const Context&, const Values& in) override {
    std::string text = in.contains("text") ? value_to_text(in.at("text")) : "";
    std::string find = in.contains("find") ? value_to_text(in.at("find")) : default_find_;
    std::string rep = in.contains("replace") ? value_to_text(in.at("replace")) : default_rep_;

    if (find.empty()) return {{"y", text}};

    std::string out;
    if (all_) {
      std::size_t start = 0;
      while (start < text.size()) {
        std::size_t pos = text.find(find, start);
        if (pos == std::string::npos) {
          out.append(text, start, text.size() - start);
          break;
        }
        out.append(text, start, pos - start);
        out.append(rep);
        start = pos + find.size();
      }
    } else {
      std::size_t pos = text.find(find);
      if (pos != std::string::npos) {
        out = text.substr(0, pos) + rep + text.substr(pos + find.size());
      } else {
        out = text;
      }
    }
    return {{"y", out}};
  }

 private:
  std::string default_find_;
  std::string default_rep_;
  bool all_;
};

class Substring final : public Block {
 public:
  Substring(double start, double length) : default_start_(start), default_length_(length) {}

  Values step(const Context&, const Values& in) override {
    std::string text = in.contains("text") ? value_to_text(in.at("text")) : "";
    double start_d = in.contains("start") ? get_or(in, "start") : default_start_;
    double len_d = in.contains("length") ? get_or(in, "length") : default_length_;

    long long n = static_cast<long long>(text.size());
    long long start = static_cast<long long>(start_d);
    if (start < 0) start += n;
    if (start < 0) start = 0;
    if (start > n) start = n;

    long long len;
    if (len_d < 0) {
      len = n - start;
    } else {
      len = std::min(static_cast<long long>(len_d), n - start);
    }
    return {{"y", text.substr(static_cast<std::size_t>(start), static_cast<std::size_t>(len))}};
  }

 private:
  double default_start_;
  double default_length_;
};

class Trim final : public Block {
 public:
  explicit Trim(int mode) : mode_(mode) {}

  Values step(const Context&, const Values& in) override {
    std::string text = in.contains("text") ? value_to_text(in.at("text")) : "";
    std::string_view sv = text;
    if (mode_ == 0 || mode_ == 1) {
      while (!sv.empty() && std::isspace(static_cast<unsigned char>(sv.front()))) sv.remove_prefix(1);
    }
    if (mode_ == 0 || mode_ == 2) {
      while (!sv.empty() && std::isspace(static_cast<unsigned char>(sv.back()))) sv.remove_suffix(1);
    }
    return {{"y", std::string(sv)}};
  }

 private:
  int mode_;
};

class ChangeCase final : public Block {
 public:
  explicit ChangeCase(int mode) : mode_(mode) {}

  Values step(const Context&, const Values& in) override {
    std::string text = in.contains("text") ? value_to_text(in.at("text")) : "";
    if (mode_ == 0) {
      for (char& c : text) c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
    } else if (mode_ == 1) {
      for (char& c : text) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    } else if (mode_ == 2) {
      bool new_word = true;
      for (char& c : text) {
        if (std::isspace(static_cast<unsigned char>(c)) || std::ispunct(static_cast<unsigned char>(c))) {
          new_word = true;
        } else if (new_word) {
          c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
          new_word = false;
        } else {
          c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
        }
      }
    }
    return {{"y", text}};
  }

 private:
  int mode_;
};

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

void add_strings(Registry& registry) {
  // string/concat
  auto concat = make_template(
      "string", "concat", "Concat", {"a", "b"}, {"y"},
      {{.id = "separator", .type = "text", .default_text = ""},
       {.id = "inputs", .default_value = 2.0, .type = "number"}},
      [](const Values& p, const Config& c) {
        std::string sep = c.contains("separator") ? c.at("separator") : "";
        int n = std::clamp(static_cast<int>(get_or(p, "inputs", 2.0)), 1, 26);
        return std::make_unique<Concat>(std::move(sep), n);
      },
      "Concatenates its inputs formatted as text.", {.compact = true, .glyph = "++", .value = "{separator}"});
  for (auto& inp : concat.def.inputs) inp.type = "any";
  concat.def.outputs[0].type = "text";
  concat.def.ports_from = "inputs";
  registry.add(std::move(concat));

  // string/format
  Template fmt;
  fmt.name = "string/format";
  fmt.def.type = "format";
  fmt.def.label = "Format";
  fmt.def.kind = Kind::Function;
  fmt.def.outputs = {{.id = "y", .type = "text"}};
  fmt.def.params = {{.id = "template", .type = "text", .default_text = "Hello {name}"}};
  fmt.def.description = "Fills template string with values from dynamic input sockets.";
  fmt.def.ports_from = "template";
  fmt.def.view = {.compact = true, .glyph = "{…}", .value = "{template}"};
  fmt.make = [](const Values&, const Config& c) {
    std::string tmpl = c.contains("template") ? c.at("template") : "Hello {name}";
    return std::make_unique<Format>(std::move(tmpl));
  };
  registry.add(std::move(fmt));

  // string/split
  Template split;
  split.name = "string/split";
  split.def.type = "split";
  split.def.label = "Split";
  split.def.kind = Kind::Function;
  split.def.inputs = {{.id = "text", .type = "text"}, {.id = "delimiter", .type = "text"}};
  split.def.outputs = {{.id = "y", .type = "text"}, {.id = "vector", .type = "vector"}, {.id = "count", .type = "signal"}};
  split.def.params = {{.id = "delimiter", .type = "text", .default_text = ","},
                      {.id = "index", .default_value = -1.0, .type = "number"}};
  split.def.description = "Splits text by delimiter into tokens, a numeric vector, and count.";
  split.def.view = {.compact = true, .glyph = "a|b", .value = "{delimiter}"};
  split.make = [](const Values& p, const Config& c) {
    std::string delim = c.contains("delimiter") ? c.at("delimiter") : ",";
    double index = get_or(p, "index", -1.0);
    return std::make_unique<Split>(std::move(delim), index);
  };
  registry.add(std::move(split));

  // string/replace
  Template rep;
  rep.name = "string/replace";
  rep.def.type = "replace";
  rep.def.label = "Replace";
  rep.def.kind = Kind::Function;
  rep.def.inputs = {{.id = "text", .type = "text"}, {.id = "find", .type = "text"}, {.id = "replace", .type = "text"}};
  rep.def.outputs = {{.id = "y", .type = "text"}};
  rep.def.params = {{.id = "find", .type = "text", .default_text = ""},
                    {.id = "replace", .type = "text", .default_text = ""},
                    {.id = "all", .default_value = 1.0, .type = "boolean"}};
  rep.def.description = "Replaces occurrences of find with replace in text.";
  rep.def.view = {.compact = true, .glyph = "a→b", .value = "{find}→{replace}"};
  rep.make = [](const Values& p, const Config& c) {
    std::string find = c.contains("find") ? c.at("find") : "";
    std::string replace = c.contains("replace") ? c.at("replace") : "";
    bool all = get_or(p, "all", 1.0) != 0.0;
    return std::make_unique<Replace>(std::move(find), std::move(replace), all);
  };
  registry.add(std::move(rep));

  // string/substring
  Template sub;
  sub.name = "string/substring";
  sub.def.type = "substring";
  sub.def.label = "Substring";
  sub.def.kind = Kind::Function;
  sub.def.inputs = {{.id = "text", .type = "text"}, {.id = "start", .type = "signal"}, {.id = "length", .type = "signal"}};
  sub.def.outputs = {{.id = "y", .type = "text"}};
  sub.def.params = {{.id = "start", .default_value = 0.0, .type = "number"},
                    {.id = "length", .default_value = -1.0, .type = "number"}};
  sub.def.description = "Extracts a substring from text.";
  sub.def.view = {.compact = true, .glyph = "[..]", .value = "{start}:{length}"};
  sub.make = [](const Values& p, const Config&) {
    double start = get_or(p, "start", 0.0);
    double len = get_or(p, "length", -1.0);
    return std::make_unique<Substring>(start, len);
  };
  registry.add(std::move(sub));

  // string/trim
  Template trim;
  trim.name = "string/trim";
  trim.def.type = "trim";
  trim.def.label = "Trim";
  trim.def.kind = Kind::Function;
  trim.def.inputs = {{.id = "text", .type = "text"}};
  trim.def.outputs = {{.id = "y", .type = "text"}};
  trim.def.params = {{.id = "mode", .type = "enum", .default_text = "both", .options = {"both", "start", "end"}}};
  trim.def.description = "Trims leading and/or trailing whitespace from text.";
  trim.def.view = {.compact = true, .glyph = ">|<", .value = "{mode}"};
  trim.make = [](const Values& p, const Config&) {
    int mode = static_cast<int>(get_or(p, "mode", 0.0));
    return std::make_unique<Trim>(mode);
  };
  registry.add(std::move(trim));

  // string/length
  Template len;
  len.name = "string/length";
  len.def.type = "length";
  len.def.label = "Length";
  len.def.kind = Kind::Function;
  len.def.inputs = {{.id = "text", .type = "text"}};
  len.def.outputs = {{.id = "y", .type = "signal"}};
  len.def.description = "Returns the character count of text.";
  len.def.view = {.compact = true, .glyph = "#T"};
  len.make = [](const Values&, const Config&) {
    return std::make_unique<Stateless>([](const Context&, const Values& in) {
      std::string text = in.contains("text") ? value_to_text(in.at("text")) : "";
      return Values{{"y", static_cast<double>(text.size())}};
    });
  };
  registry.add(std::move(len));

  // string/change_case
  Template cc;
  cc.name = "string/change_case";
  cc.def.type = "change_case";
  cc.def.label = "Change Case";
  cc.def.kind = Kind::Function;
  cc.def.inputs = {{.id = "text", .type = "text"}};
  cc.def.outputs = {{.id = "y", .type = "text"}};
  cc.def.params = {{.id = "mode", .type = "enum", .default_text = "lower", .options = {"upper", "lower", "title"}}};
  cc.def.description = "Converts text to upper, lower, or title case.";
  cc.def.view = {.compact = true, .glyph = "aA", .value = "{mode}"};
  cc.make = [](const Values& p, const Config&) {
    int mode = static_cast<int>(get_or(p, "mode", 1.0));
    return std::make_unique<ChangeCase>(mode);
  };
  registry.add(std::move(cc));

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
