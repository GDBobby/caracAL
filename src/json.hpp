#pragma once

#include <cctype>
#include <cstdio>
#include <cmath>
#include <cstdint>
#include <map>
#include <optional>
#include <sstream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <variant>
#include <vector>

namespace caracal {

class Json {
 public:
  using object_t = std::map<std::string, Json>;
  using array_t = std::vector<Json>;

  Json() : data_(nullptr) {}
  Json(std::nullptr_t) : data_(nullptr) {}
  Json(bool v) : data_(v) {}
  Json(int v) : data_(static_cast<double>(v)) {}
  Json(unsigned v) : data_(static_cast<double>(v)) {}
  Json(long v) : data_(static_cast<double>(v)) {}
  Json(long long v) : data_(static_cast<double>(v)) {}
  Json(double v) : data_(v) {}
  Json(const char* v) : data_(std::string(v ? v : "")) {}
  Json(std::string v) : data_(std::move(v)) {}
  Json(std::string_view v) : data_(std::string(v)) {}
  Json(object_t v) : data_(std::move(v)) {}
  Json(array_t v) : data_(std::move(v)) {}

  static Json object() { return Json(object_t{}); }
  static Json array() { return Json(array_t{}); }

  bool is_null() const { return std::holds_alternative<std::nullptr_t>(data_); }
  bool is_bool() const { return std::holds_alternative<bool>(data_); }
  bool is_number() const { return std::holds_alternative<double>(data_); }
  bool is_string() const { return std::holds_alternative<std::string>(data_); }
  bool is_array() const { return std::holds_alternative<array_t>(data_); }
  bool is_object() const { return std::holds_alternative<object_t>(data_); }

  bool as_bool() const {
    if (auto* p = std::get_if<bool>(&data_)) return *p;
    throw std::runtime_error("json: not a bool");
  }
  double as_number() const {
    if (auto* p = std::get_if<double>(&data_)) return *p;
    throw std::runtime_error("json: not a number");
  }
  const std::string& as_string() const {
    if (auto* p = std::get_if<std::string>(&data_)) return *p;
    throw std::runtime_error("json: not a string");
  }
  const array_t& as_array() const {
    if (auto* p = std::get_if<array_t>(&data_)) return *p;
    throw std::runtime_error("json: not an array");
  }
  array_t& as_array() {
    if (auto* p = std::get_if<array_t>(&data_)) return *p;
    throw std::runtime_error("json: not an array");
  }
  const object_t& as_object() const {
    if (auto* p = std::get_if<object_t>(&data_)) return *p;
    throw std::runtime_error("json: not an object");
  }
  object_t& as_object() {
    if (auto* p = std::get_if<object_t>(&data_)) return *p;
    throw std::runtime_error("json: not an object");
  }

  Json& operator[](const std::string& key) {
    if (!is_object()) data_ = object_t{};
    return as_object()[key];
  }
  const Json& operator[](const std::string& key) const {
    static const Json none;
    if (!is_object()) return none;
    auto it = as_object().find(key);
    return it == as_object().end() ? none : it->second;
  }
  Json& operator[](std::size_t i) {
    if (!is_array()) throw std::runtime_error("json: not an array");
    return as_array().at(i);
  }
  const Json& operator[](std::size_t i) const {
    if (!is_array()) throw std::runtime_error("json: not an array");
    return as_array().at(i);
  }

  bool contains(const std::string& key) const {
    return is_object() && as_object().contains(key);
  }

  std::string dump(int indent = -1) const { return dump_impl(indent, 0); }

  static Json parse(std::string_view text) {
    Parser p(text);
    Json v = p.parse_value();
    p.skip_ws();
    if (p.pos != p.in.size()) {
      throw std::runtime_error("json: trailing data");
    }
    return v;
  }

  std::optional<std::string> as_string_opt() const {
    if (is_string()) return as_string();
    return std::nullopt;
  }

  std::optional<double> as_number_opt() const {
    if (is_number()) return as_number();
    return std::nullopt;
  }

  bool truthy() const {
    if (is_null()) return false;
    if (is_bool()) return as_bool();
    if (is_number()) return as_number() != 0;
    if (is_string()) return !as_string().empty();
    return true;
  }

 private:
  std::variant<std::nullptr_t, bool, double, std::string, array_t, object_t>
      data_;

  struct Parser {
    std::string_view in;
    std::size_t pos = 0;
    explicit Parser(std::string_view s) : in(s) {}

    void skip_ws() {
      while (pos < in.size() && std::isspace(static_cast<unsigned char>(in[pos]))) {
        ++pos;
      }
    }
    char peek() {
      skip_ws();
      if (pos >= in.size()) throw std::runtime_error("json: unexpected end");
      return in[pos];
    }
    char take() {
      char c = peek();
      ++pos;
      return c;
    }
    bool consume(char c) {
      skip_ws();
      if (pos < in.size() && in[pos] == c) {
        ++pos;
        return true;
      }
      return false;
    }

    Json parse_value() {
      char c = peek();
      if (c == 'n') return parse_lit("null", Json(nullptr));
      if (c == 't') return parse_lit("true", Json(true));
      if (c == 'f') return parse_lit("false", Json(false));
      if (c == '"') return Json(parse_string());
      if (c == '{') return parse_object();
      if (c == '[') return parse_array();
      if (c == '-' || std::isdigit(static_cast<unsigned char>(c))) return parse_number();
      throw std::runtime_error(std::string("json: unexpected '") + c + "'");
    }

    Json parse_lit(std::string_view lit, Json v) {
      skip_ws();
      if (in.substr(pos, lit.size()) != lit) {
        throw std::runtime_error("json: invalid literal");
      }
      pos += lit.size();
      return v;
    }

    std::string parse_string() {
      if (take() != '"') throw std::runtime_error("json: expected string");
      std::string out;
      while (pos < in.size()) {
        char c = in[pos++];
        if (c == '"') return out;
        if (c == '\\') {
          if (pos >= in.size()) throw std::runtime_error("json: bad escape");
          char e = in[pos++];
          switch (e) {
            case '"':
            case '\\':
            case '/':
              out.push_back(e);
              break;
            case 'b':
              out.push_back('\b');
              break;
            case 'f':
              out.push_back('\f');
              break;
            case 'n':
              out.push_back('\n');
              break;
            case 'r':
              out.push_back('\r');
              break;
            case 't':
              out.push_back('\t');
              break;
            case 'u': {
              if (pos + 4 > in.size()) throw std::runtime_error("json: bad unicode");
              unsigned cp = 0;
              for (int i = 0; i < 4; ++i) {
                char h = in[pos++];
                cp <<= 4;
                if (h >= '0' && h <= '9') cp += static_cast<unsigned>(h - '0');
                else if (h >= 'a' && h <= 'f') cp += static_cast<unsigned>(h - 'a' + 10);
                else if (h >= 'A' && h <= 'F') cp += static_cast<unsigned>(h - 'A' + 10);
                else throw std::runtime_error("json: bad unicode");
              }
              if (cp < 0x80) {
                out.push_back(static_cast<char>(cp));
              } else if (cp < 0x800) {
                out.push_back(static_cast<char>(0xC0 | (cp >> 6)));
                out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
              } else {
                out.push_back(static_cast<char>(0xE0 | (cp >> 12)));
                out.push_back(static_cast<char>(0x80 | ((cp >> 6) & 0x3F)));
                out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
              }
              break;
            }
            default:
              throw std::runtime_error("json: bad escape");
          }
        } else {
          out.push_back(c);
        }
      }
      throw std::runtime_error("json: unterminated string");
    }

    Json parse_number() {
      skip_ws();
      const std::size_t start = pos;
      if (pos < in.size() && in[pos] == '-') ++pos;
      if (pos < in.size() && in[pos] == '0') {
        ++pos;
      } else {
        if (pos >= in.size() || !std::isdigit(static_cast<unsigned char>(in[pos]))) {
          throw std::runtime_error("json: bad number");
        }
        while (pos < in.size() && std::isdigit(static_cast<unsigned char>(in[pos]))) ++pos;
      }
      if (pos < in.size() && in[pos] == '.') {
        ++pos;
        if (pos >= in.size() || !std::isdigit(static_cast<unsigned char>(in[pos]))) {
          throw std::runtime_error("json: bad number");
        }
        while (pos < in.size() && std::isdigit(static_cast<unsigned char>(in[pos]))) ++pos;
      }
      if (pos < in.size() && (in[pos] == 'e' || in[pos] == 'E')) {
        ++pos;
        if (pos < in.size() && (in[pos] == '+' || in[pos] == '-')) ++pos;
        if (pos >= in.size() || !std::isdigit(static_cast<unsigned char>(in[pos]))) {
          throw std::runtime_error("json: bad number");
        }
        while (pos < in.size() && std::isdigit(static_cast<unsigned char>(in[pos]))) ++pos;
      }
      return Json(std::stod(std::string(in.substr(start, pos - start))));
    }

    Json parse_array() {
      if (take() != '[') throw std::runtime_error("json: expected array");
      array_t arr;
      skip_ws();
      if (consume(']')) return Json(std::move(arr));
      while (true) {
        arr.push_back(parse_value());
        if (consume(']')) return Json(std::move(arr));
        if (!consume(',')) throw std::runtime_error("json: expected comma");
      }
    }

    Json parse_object() {
      if (take() != '{') throw std::runtime_error("json: expected object");
      object_t obj;
      skip_ws();
      if (consume('}')) return Json(std::move(obj));
      while (true) {
        if (peek() != '"') throw std::runtime_error("json: expected key");
        std::string key = parse_string();
        if (!consume(':')) throw std::runtime_error("json: expected colon");
        obj.emplace(std::move(key), parse_value());
        if (consume('}')) return Json(std::move(obj));
        if (!consume(',')) throw std::runtime_error("json: expected comma");
      }
    }
  };

  std::string dump_impl(int indent, int depth) const {
    if (is_null()) return "null";
    if (is_bool()) return as_bool() ? "true" : "false";
    if (is_number()) {
      double n = as_number();
      if (std::isfinite(n) && n == std::floor(n) && std::abs(n) < 1e15) {
        return std::to_string(static_cast<long long>(n));
      }
      std::ostringstream os;
      os << n;
      return os.str();
    }
    if (is_string()) return quote(as_string());
    const std::string nl = indent >= 0 ? "\n" : "";
    const std::string pad(indent >= 0 ? static_cast<std::size_t>(indent * (depth + 1)) : 0, ' ');
    const std::string pad0(indent >= 0 ? static_cast<std::size_t>(indent * depth) : 0, ' ');
    const std::string sep = indent >= 0 ? ",\n" : ",";
    const std::string kv = indent >= 0 ? ": " : ":";
    if (is_array()) {
      const auto& a = as_array();
      if (a.empty()) return "[]";
      std::string out = "[" + nl;
      for (std::size_t i = 0; i < a.size(); ++i) {
        out += pad + a[i].dump_impl(indent, depth + 1);
        if (i + 1 < a.size()) out += sep;
        else out += nl;
      }
      out += pad0 + "]";
      return out;
    }
    const auto& o = as_object();
    if (o.empty()) return "{}";
    std::string out = "{" + nl;
    std::size_t i = 0;
    for (const auto& [k, v] : o) {
      out += pad + quote(k) + kv + v.dump_impl(indent, depth + 1);
      if (++i < o.size()) out += sep;
      else out += nl;
    }
    out += pad0 + "}";
    return out;
  }

  static std::string quote(std::string_view s) {
    std::string out = "\"";
    for (unsigned char c : s) {
      switch (c) {
        case '"':
          out += "\\\"";
          break;
        case '\\':
          out += "\\\\";
          break;
        case '\b':
          out += "\\b";
          break;
        case '\f':
          out += "\\f";
          break;
        case '\n':
          out += "\\n";
          break;
        case '\r':
          out += "\\r";
          break;
        case '\t':
          out += "\\t";
          break;
        default:
          if (c < 0x20) {
            char buf[8];
            std::snprintf(buf, sizeof(buf), "\\u%04x", c);
            out += buf;
          } else {
            out.push_back(static_cast<char>(c));
          }
      }
    }
    out += '"';
    return out;
  }
};

}  // namespace caracal
