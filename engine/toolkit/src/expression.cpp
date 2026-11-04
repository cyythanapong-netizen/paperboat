#include "paperboat/toolkit/expression.hpp"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <map>
#include <numbers>

namespace paperboat::toolkit {

struct Expression::Node {
  enum class Op { Number, Variable, Neg, Not, Add, Sub, Mul, Div, Mod, Pow, Lt, Le, Gt, Ge, Eq, Ne, And, Or, If, Call };
  Op op = Op::Number;
  double number = 0;
  std::size_t index = 0;      // a variable's
  double (*fn1)(double) = nullptr;
  double (*fn2)(double, double) = nullptr;
  bool clamp = false;         // clamp(x, lo, hi)
  std::vector<std::shared_ptr<const Node>> kids;
};

namespace {

using Node = Expression::Node;
using Op = Node::Op;
using Ptr = std::shared_ptr<const Node>;

double frac(double x) { return x - std::floor(x); }
double sign(double x) { return (x > 0) - (x < 0); }
double fmin2(double a, double b) { return std::min(a, b); }
double fmax2(double a, double b) { return std::max(a, b); }
double sin1(double x) { return std::sin(x); }
double cos1(double x) { return std::cos(x); }
double tan1(double x) { return std::tan(x); }
double asin1(double x) { return std::asin(x); }
double acos1(double x) { return std::acos(x); }
double atan1(double x) { return std::atan(x); }
double sinh1(double x) { return std::sinh(x); }
double cosh1(double x) { return std::cosh(x); }
double tanh1(double x) { return std::tanh(x); }
double exp1(double x) { return std::exp(x); }
double log1(double x) { return std::log(x); }
double log101(double x) { return std::log10(x); }
double sqrt1(double x) { return std::sqrt(x); }
double abs1(double x) { return std::fabs(x); }
double floor1(double x) { return std::floor(x); }
double ceil1(double x) { return std::ceil(x); }
double round1(double x) { return std::round(x); }
double atan22(double y, double x) { return std::atan2(y, x); }
double fmod2(double a, double b) { return std::fmod(a, b); }
double pow2(double a, double b) { return std::pow(a, b); }

const std::map<std::string, double (*)(double), std::less<>> kOne = {
    {"sin", sin1},   {"cos", cos1},     {"tan", tan1},     {"asin", asin1}, {"acos", acos1},   {"atan", atan1},
    {"sinh", sinh1}, {"cosh", cosh1},   {"tanh", tanh1},   {"exp", exp1},   {"log", log1},     {"log10", log101},
    {"sqrt", sqrt1}, {"abs", abs1},     {"floor", floor1}, {"ceil", ceil1}, {"round", round1}, {"sign", sign},
    {"frac", frac}};
const std::map<std::string, double (*)(double, double), std::less<>> kTwo = {
    {"atan2", atan22}, {"min", fmin2}, {"max", fmax2}, {"mod", fmod2}, {"pow", pow2}};

Ptr make(Op op, std::vector<Ptr> kids = {}) {
  auto n = std::make_shared<Node>();
  n->op = op;
  n->kids = std::move(kids);
  return n;
}

class Parser {
 public:
  Parser(std::string_view text, const std::vector<std::string>& variables) : s_(text), vars_(variables) {}

  Ptr parse() {
    Ptr e = ternary();
    space();
    if (i_ < s_.size()) fail("unexpected \"" + std::string(1, s_[i_]) + "\"");
    return e;
  }

 private:
  [[noreturn]] void fail(const std::string& what) const {
    throw ExpressionError("at " + std::to_string(i_ + 1) + ": " + what);
  }
  void space() {
    while (i_ < s_.size() && std::isspace(static_cast<unsigned char>(s_[i_]))) ++i_;
  }
  bool eat(std::string_view token) {
    space();
    if (s_.substr(i_, token.size()) != token) return false;
    // "<" must not eat the start of "<=", "!" not of "!=", "=" doesn't exist alone.
    if (token.size() == 1 && i_ + 1 < s_.size() && s_[i_ + 1] == '=' && std::string_view("<>!").find(token[0]) != std::string_view::npos)
      return false;
    i_ += token.size();
    return true;
  }
  void expect(std::string_view token) {
    if (!eat(token)) fail("expected \"" + std::string(token) + "\"");
  }

  Ptr ternary() {
    Ptr c = logic_or();
    if (!eat("?")) return c;
    Ptr a = ternary();
    expect(":");
    Ptr b = ternary();
    return make(Op::If, {c, a, b});
  }
  Ptr logic_or() {
    Ptr a = logic_and();
    while (eat("||")) a = make(Op::Or, {a, logic_and()});
    return a;
  }
  Ptr logic_and() {
    Ptr a = compare();
    while (eat("&&")) a = make(Op::And, {a, compare()});
    return a;
  }
  Ptr compare() {
    Ptr a = sum();
    for (;;) {
      if (eat("<=")) a = make(Op::Le, {a, sum()});
      else if (eat(">=")) a = make(Op::Ge, {a, sum()});
      else if (eat("==")) a = make(Op::Eq, {a, sum()});
      else if (eat("!=")) a = make(Op::Ne, {a, sum()});
      else if (eat("<")) a = make(Op::Lt, {a, sum()});
      else if (eat(">")) a = make(Op::Gt, {a, sum()});
      else return a;
    }
  }
  Ptr sum() {
    Ptr a = product();
    for (;;) {
      if (eat("+")) a = make(Op::Add, {a, product()});
      else if (eat("-")) a = make(Op::Sub, {a, product()});
      else return a;
    }
  }
  Ptr product() {
    Ptr a = unary();
    for (;;) {
      if (eat("*")) a = make(Op::Mul, {a, unary()});
      else if (eat("/")) a = make(Op::Div, {a, unary()});
      else if (eat("%")) a = make(Op::Mod, {a, unary()});
      else return a;
    }
  }
  Ptr unary() {
    if (eat("-")) return make(Op::Neg, {unary()});
    if (eat("+")) return unary();
    if (eat("!")) return make(Op::Not, {unary()});
    return power();
  }
  Ptr power() {
    Ptr base = primary();
    if (eat("^")) return make(Op::Pow, {base, unary()});  // right to left: 2^3^2 = 2^9
    return base;
  }
  Ptr primary() {
    space();
    if (i_ >= s_.size()) fail("expected a number, a name or \"(\"");
    const char c = s_[i_];
    if (eat("(")) {
      Ptr e = ternary();
      expect(")");
      return e;
    }
    if (std::isdigit(static_cast<unsigned char>(c)) || c == '.') {
      std::size_t used = 0;
      const std::string rest(s_.substr(i_));
      double v = 0;
      try {
        v = std::stod(rest, &used);
      } catch (const std::exception&) {
        fail("bad number");
      }
      i_ += used;
      auto n = std::make_shared<Node>();
      n->number = v;
      return n;
    }
    if (std::isalpha(static_cast<unsigned char>(c)) || c == '_') {
      const std::size_t start = i_;
      while (i_ < s_.size() && (std::isalnum(static_cast<unsigned char>(s_[i_])) || s_[i_] == '_')) ++i_;
      const std::string name(s_.substr(start, i_ - start));
      if (eat("(")) return call(name, start);
      if (auto it = std::ranges::find(vars_, name); it != vars_.end()) {
        auto n = std::make_shared<Node>();
        n->op = Op::Variable;
        n->index = static_cast<std::size_t>(it - vars_.begin());
        return n;
      }
      auto n = std::make_shared<Node>();
      if (name == "pi") n->number = std::numbers::pi;
      else if (name == "e") n->number = std::numbers::e;
      else {
        i_ = start;
        std::string known;
        for (const auto& v : vars_) known += (known.empty() ? "" : ", ") + v;
        fail("unknown name \"" + name + "\" (known: " + known + (known.empty() ? "" : ", ") + "pi, e)");
      }
      return n;
    }
    fail("unexpected \"" + std::string(1, c) + "\"");
  }
  Ptr call(const std::string& name, std::size_t start) {
    std::vector<Ptr> args;
    if (!eat(")")) {
      do args.push_back(ternary());
      while (eat(","));
      expect(")");
    }
    auto n = std::make_shared<Node>();
    n->op = Op::Call;
    n->kids = args;
    const auto arity = [&](std::size_t want) {
      if (args.size() != want) {
        i_ = start;
        fail(name + "() takes " + std::to_string(want) + " argument" + (want == 1 ? "" : "s"));
      }
    };
    if (auto f = kOne.find(name); f != kOne.end()) {
      arity(1);
      n->fn1 = f->second;
    } else if (auto g = kTwo.find(name); g != kTwo.end()) {
      arity(2);
      n->fn2 = g->second;
    } else if (name == "clamp") {
      arity(3);
      n->clamp = true;
    } else {
      i_ = start;
      fail("unknown function \"" + name + "\"");
    }
    return n;
  }

  std::string_view s_;
  const std::vector<std::string>& vars_;
  std::size_t i_ = 0;
};

double eval(const Node& n, std::span<const double> v) {
  auto k = [&](std::size_t i) { return eval(*n.kids[i], v); };
  switch (n.op) {
    case Op::Number: return n.number;
    case Op::Variable: return n.index < v.size() ? v[n.index] : 0.0;
    case Op::Neg: return -k(0);
    case Op::Not: return k(0) == 0 ? 1.0 : 0.0;
    case Op::Add: return k(0) + k(1);
    case Op::Sub: return k(0) - k(1);
    case Op::Mul: return k(0) * k(1);
    case Op::Div: return k(0) / k(1);
    case Op::Mod: return std::fmod(k(0), k(1));
    case Op::Pow: return std::pow(k(0), k(1));
    case Op::Lt: return k(0) < k(1);
    case Op::Le: return k(0) <= k(1);
    case Op::Gt: return k(0) > k(1);
    case Op::Ge: return k(0) >= k(1);
    case Op::Eq: return k(0) == k(1);
    case Op::Ne: return k(0) != k(1);
    case Op::And: return k(0) != 0 && k(1) != 0;
    case Op::Or: return k(0) != 0 || k(1) != 0;
    case Op::If: return k(0) != 0 ? k(1) : k(2);
    case Op::Call:
      if (n.fn1) return n.fn1(k(0));
      if (n.fn2) return n.fn2(k(0), k(1));
      return std::min(std::max(k(0), k(1)), k(2));  // clamp, safe when lo > hi
  }
  return 0;
}

}  // namespace

Expression::Expression(std::string_view text, std::vector<std::string> variables)
    : text_(text), variables_(std::move(variables)) {
  if (text_.find_first_not_of(" \t\n") == std::string::npos) throw ExpressionError("empty expression");
  root_ = Parser(text_, variables_).parse();
}

double Expression::operator()(std::span<const double> values) const { return eval(*root_, values); }

bool Expression::is_builtin(std::string_view name) {
  return name == "pi" || name == "e" || name == "clamp" || kOne.contains(name) || kTwo.contains(name);
}

}  // namespace paperboat::toolkit
