#pragma once

#include <cstddef>
#include <map>
#include <stdexcept>
#include <string>
#include <vector>

namespace ps5h {

class JsonError : public std::runtime_error {
 public:
  explicit JsonError(const std::string& what) : std::runtime_error(what) {}
};

class Json {
 public:
  enum class Type { Null, Bool, Number, String, Array, Object };

  Json() = default;
  Json(std::nullptr_t) {}
  Json(bool value) : type_(Type::Bool), bool_(value) {}
  Json(int value) : type_(Type::Number), number_(value) {}
  Json(long long value) : type_(Type::Number), number_(static_cast<double>(value)) {}
  Json(double value) : type_(Type::Number), number_(value) {}
  Json(const char* value) : type_(Type::String), string_(value) {}
  Json(std::string value) : type_(Type::String), string_(std::move(value)) {}

  static Json object();
  static Json array();

  Type type() const { return type_; }
  bool is_null() const { return type_ == Type::Null; }
  bool is_bool() const { return type_ == Type::Bool; }
  bool is_number() const { return type_ == Type::Number; }
  bool is_string() const { return type_ == Type::String; }
  bool is_array() const { return type_ == Type::Array; }
  bool is_object() const { return type_ == Type::Object; }

  bool as_bool(bool fallback = false) const { return type_ == Type::Bool ? bool_ : fallback; }
  double as_number(double fallback = 0.0) const { return type_ == Type::Number ? number_ : fallback; }
  std::string as_string(const std::string& fallback = std::string()) const {
    return type_ == Type::String ? string_ : fallback;
  }

  const Json& operator[](const std::string& key) const;
  Json& operator[](const std::string& key);
  const Json& operator[](std::size_t index) const;
  Json& operator[](std::size_t index);

  std::size_t size() const;
  void push_back(Json value);
  bool contains(const std::string& key) const;

  const std::vector<Json>& items() const { return array_; }
  std::vector<Json>& items() { return array_; }
  const std::map<std::string, Json>& fields() const { return object_; }
  std::map<std::string, Json>& fields() { return object_; }

  // Walks a dotted path ("response.docs.0.identifier"). Missing paths yield null.
  Json pick(const std::string& dotted_path) const;

  std::string dump(int indent = -1) const;

  static Json parse(const std::string& text);

 private:
  void dump_to(std::string& out, int indent, int depth) const;

  Type type_ = Type::Null;
  bool bool_ = false;
  double number_ = 0.0;
  std::string string_;
  std::vector<Json> array_;
  std::map<std::string, Json> object_;
};

}  // namespace ps5h
