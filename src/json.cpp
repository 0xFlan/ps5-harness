#include "ps5h/json.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>

namespace ps5h {
namespace {

const Json& null_value() {
  static const Json value;
  return value;
}

std::string format_number(double value) {
  char buffer[48];
  if (std::isfinite(value) && value == std::floor(value) && std::fabs(value) < 1e15) {
    std::snprintf(buffer, sizeof buffer, "%lld", static_cast<long long>(value));
    return buffer;
  }
  std::snprintf(buffer, sizeof buffer, "%.17g", value);
  return buffer;
}

void escape_into(std::string& out, const std::string& value) {
  out.push_back('"');
  for (unsigned char c : value) {
    switch (c) {
      case '"': out += "\\\""; break;
      case '\\': out += "\\\\"; break;
      case '\n': out += "\\n"; break;
      case '\r': out += "\\r"; break;
      case '\t': out += "\\t"; break;
      case '\b': out += "\\b"; break;
      case '\f': out += "\\f"; break;
      default:
        if (c < 0x20) {
          char buffer[8];
          std::snprintf(buffer, sizeof buffer, "\\u%04x", c);
          out += buffer;
        } else {
          out.push_back(static_cast<char>(c));
        }
    }
  }
  out.push_back('"');
}

class Parser {
 public:
  explicit Parser(const std::string& text) : text_(text) {}

  Json parse_document() {
    Json value = parse_value();
    skip_space();
    if (position_ != text_.size()) {
      fail("trailing characters");
    }
    return value;
  }

 private:
  [[noreturn]] void fail(const std::string& message) const {
    throw JsonError(message + " at offset " + std::to_string(position_));
  }

  void skip_space() {
    while (position_ < text_.size()) {
      char c = text_[position_];
      if (c == ' ' || c == '\t' || c == '\n' || c == '\r') {
        ++position_;
      } else {
        break;
      }
    }
  }

  char peek() const {
    if (position_ >= text_.size()) {
      const_cast<Parser*>(this)->fail("unexpected end of input");
    }
    return text_[position_];
  }

  void expect(char c) {
    skip_space();
    if (peek() != c) {
      fail(std::string("expected '") + c + "'");
    }
    ++position_;
  }

  Json parse_value() {
    skip_space();
    if (++depth_ > 128) {
      fail("nesting too deep");
    }
    Json value;
    switch (peek()) {
      case '{': value = parse_object(); break;
      case '[': value = parse_array(); break;
      case '"': value = Json(parse_string()); break;
      case 't':
        parse_literal("true");
        value = Json(true);
        break;
      case 'f':
        parse_literal("false");
        value = Json(false);
        break;
      case 'n':
        parse_literal("null");
        value = Json();
        break;
      default: value = Json(parse_number()); break;
    }
    --depth_;
    return value;
  }

  void parse_literal(const char* literal) {
    std::size_t length = std::strlen(literal);
    if (text_.compare(position_, length, literal) != 0) {
      fail("invalid literal");
    }
    position_ += length;
  }

  double parse_number() {
    skip_space();
    std::size_t start = position_;
    if (position_ < text_.size() && (text_[position_] == '-' || text_[position_] == '+')) {
      ++position_;
    }
    bool any_digit = false;
    while (position_ < text_.size() && std::isdigit(static_cast<unsigned char>(text_[position_]))) {
      ++position_;
      any_digit = true;
    }
    if (position_ < text_.size() && text_[position_] == '.') {
      ++position_;
      while (position_ < text_.size() && std::isdigit(static_cast<unsigned char>(text_[position_]))) {
        ++position_;
        any_digit = true;
      }
    }
    if (!any_digit) {
      fail("invalid number");
    }
    if (position_ < text_.size() && (text_[position_] == 'e' || text_[position_] == 'E')) {
      ++position_;
      if (position_ < text_.size() && (text_[position_] == '-' || text_[position_] == '+')) {
        ++position_;
      }
      while (position_ < text_.size() && std::isdigit(static_cast<unsigned char>(text_[position_]))) {
        ++position_;
      }
    }
    return std::strtod(text_.substr(start, position_ - start).c_str(), nullptr);
  }

  std::string parse_string() {
    expect('"');
    std::string out;
    while (true) {
      if (position_ >= text_.size()) {
        fail("unterminated string");
      }
      char c = text_[position_++];
      if (c == '"') {
        break;
      }
      if (c != '\\') {
        out.push_back(c);
        continue;
      }
      if (position_ >= text_.size()) {
        fail("unterminated escape");
      }
      char escape = text_[position_++];
      switch (escape) {
        case '"': out.push_back('"'); break;
        case '\\': out.push_back('\\'); break;
        case '/': out.push_back('/'); break;
        case 'b': out.push_back('\b'); break;
        case 'f': out.push_back('\f'); break;
        case 'n': out.push_back('\n'); break;
        case 'r': out.push_back('\r'); break;
        case 't': out.push_back('\t'); break;
        case 'u': {
          if (position_ + 4 > text_.size()) {
            fail("truncated unicode escape");
          }
          unsigned code = static_cast<unsigned>(std::strtoul(text_.substr(position_, 4).c_str(), nullptr, 16));
          position_ += 4;
          if (code < 0x80) {
            out.push_back(static_cast<char>(code));
          } else if (code < 0x800) {
            out.push_back(static_cast<char>(0xC0 | (code >> 6)));
            out.push_back(static_cast<char>(0x80 | (code & 0x3F)));
          } else {
            out.push_back(static_cast<char>(0xE0 | (code >> 12)));
            out.push_back(static_cast<char>(0x80 | ((code >> 6) & 0x3F)));
            out.push_back(static_cast<char>(0x80 | (code & 0x3F)));
          }
          break;
        }
        default: fail("unknown escape");
      }
    }
    return out;
  }

  Json parse_array() {
    Json out = Json::array();
    expect('[');
    skip_space();
    if (peek() == ']') {
      ++position_;
      return out;
    }
    while (true) {
      out.push_back(parse_value());
      skip_space();
      char c = peek();
      if (c == ',') {
        ++position_;
        continue;
      }
      if (c == ']') {
        ++position_;
        return out;
      }
      fail("expected ',' or ']'");
    }
  }

  Json parse_object() {
    Json out = Json::object();
    expect('{');
    skip_space();
    if (peek() == '}') {
      ++position_;
      return out;
    }
    while (true) {
      skip_space();
      std::string key = parse_string();
      expect(':');
      out[key] = parse_value();
      skip_space();
      char c = peek();
      if (c == ',') {
        ++position_;
        continue;
      }
      if (c == '}') {
        ++position_;
        return out;
      }
      fail("expected ',' or '}'");
    }
  }

  const std::string& text_;
  std::size_t position_ = 0;
  int depth_ = 0;
};

}  // namespace

Json Json::object() {
  Json value;
  value.type_ = Type::Object;
  return value;
}

Json Json::array() {
  Json value;
  value.type_ = Type::Array;
  return value;
}

const Json& Json::operator[](const std::string& key) const {
  if (type_ != Type::Object) {
    return null_value();
  }
  auto it = object_.find(key);
  return it == object_.end() ? null_value() : it->second;
}

Json& Json::operator[](const std::string& key) {
  if (type_ != Type::Object) {
    type_ = Type::Object;
    object_.clear();
  }
  return object_[key];
}

const Json& Json::operator[](std::size_t index) const {
  if (type_ != Type::Array || index >= array_.size()) {
    return null_value();
  }
  return array_[index];
}

Json& Json::operator[](std::size_t index) {
  if (type_ != Type::Array) {
    type_ = Type::Array;
    array_.clear();
  }
  if (index >= array_.size()) {
    array_.resize(index + 1);
  }
  return array_[index];
}

std::size_t Json::size() const {
  if (type_ == Type::Array) return array_.size();
  if (type_ == Type::Object) return object_.size();
  return 0;
}

void Json::push_back(Json value) {
  if (type_ != Type::Array) {
    type_ = Type::Array;
    array_.clear();
  }
  array_.push_back(std::move(value));
}

bool Json::contains(const std::string& key) const {
  return type_ == Type::Object && object_.find(key) != object_.end();
}

Json Json::pick(const std::string& dotted_path) const {
  if (dotted_path.empty()) {
    return *this;
  }
  const Json* cursor = this;
  std::size_t start = 0;
  while (true) {
    std::size_t dot = dotted_path.find('.', start);
    std::string key = dotted_path.substr(
        start, dot == std::string::npos ? std::string::npos : dot - start);
    if (!key.empty()) {
      if (cursor->is_array()) {
        bool numeric = std::all_of(key.begin(), key.end(),
                                   [](unsigned char c) { return std::isdigit(c) != 0; });
        if (!numeric) {
          return Json();
        }
        std::size_t index = static_cast<std::size_t>(std::strtoul(key.c_str(), nullptr, 10));
        if (index >= cursor->array_.size()) {
          return Json();
        }
        cursor = &cursor->array_[index];
      } else if (cursor->is_object()) {
        auto it = cursor->object_.find(key);
        if (it == cursor->object_.end()) {
          return Json();
        }
        cursor = &it->second;
      } else {
        return Json();
      }
    }
    if (dot == std::string::npos) {
      break;
    }
    start = dot + 1;
  }
  return *cursor;
}

void Json::dump_to(std::string& out, int indent, int depth) const {
  auto newline = [&](int level) {
    if (indent < 0) return;
    out.push_back('\n');
    out.append(static_cast<std::size_t>(indent * level), ' ');
  };
  switch (type_) {
    case Type::Null: out += "null"; break;
    case Type::Bool: out += bool_ ? "true" : "false"; break;
    case Type::Number: out += format_number(number_); break;
    case Type::String: escape_into(out, string_); break;
    case Type::Array: {
      if (array_.empty()) {
        out += "[]";
        break;
      }
      out.push_back('[');
      for (std::size_t i = 0; i < array_.size(); ++i) {
        if (i != 0) out.push_back(',');
        newline(depth + 1);
        array_[i].dump_to(out, indent, depth + 1);
      }
      newline(depth);
      out.push_back(']');
      break;
    }
    case Type::Object: {
      if (object_.empty()) {
        out += "{}";
        break;
      }
      out.push_back('{');
      std::size_t i = 0;
      for (const auto& entry : object_) {
        if (i++ != 0) out.push_back(',');
        newline(depth + 1);
        escape_into(out, entry.first);
        out.push_back(':');
        if (indent >= 0) out.push_back(' ');
        entry.second.dump_to(out, indent, depth + 1);
      }
      newline(depth);
      out.push_back('}');
      break;
    }
  }
}

std::string Json::dump(int indent) const {
  std::string out;
  dump_to(out, indent, 0);
  return out;
}

Json Json::parse(const std::string& text) {
  Parser parser(text);
  return parser.parse_document();
}

}  // namespace ps5h
