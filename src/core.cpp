#include "ps5h/core.h"

#include <cctype>
#include <chrono>
#include <cstdio>
#include <future>
#include <stdexcept>

namespace ps5h {
namespace {

bool is_name_char(unsigned char c) {
  return std::isalnum(c) != 0 || c == '_' || c == '-';
}

}  // namespace

void ToolRegistry::add(Tool tool) {
  for (auto& existing : tools_) {
    if (existing.name == tool.name) {
      existing = std::move(tool);
      return;
    }
  }
  tools_.push_back(std::move(tool));
}

const Tool* ToolRegistry::find(const std::string& name) const {
  for (const auto& tool : tools_) {
    if (tool.name == name) {
      return &tool;
    }
  }
  return nullptr;
}

Json ToolRegistry::schema_list() const {
  Json list = Json::array();
  for (const auto& tool : tools_) {
    Json schema = Json::object();
    schema["type"] = "function";
    Json function = Json::object();
    function["name"] = tool.name;
    function["description"] = tool.description;
    function["parameters"] = tool.parameters;
    schema["function"] = function;
    list.push_back(schema);
  }
  return list;
}

bool valid_tool_name(const std::string& name) {
  if (name.empty() || name.size() > 64) {
    return false;
  }
  for (unsigned char c : name) {
    if (!is_name_char(c)) {
      return false;
    }
  }
  return true;
}

std::string sanitize_tool_name(const std::string& name) {
  std::string out;
  out.reserve(name.size());
  for (unsigned char c : name) {
    out.push_back(is_name_char(c) ? static_cast<char>(std::tolower(c)) : '_');
  }
  while (!out.empty() && out.front() == '_') {
    out.erase(out.begin());
  }
  if (out.size() > 64) {
    out.resize(64);
  }
  return out;
}

std::string url_encode(const std::string& value) {
  std::string out;
  out.reserve(value.size() * 3);
  for (unsigned char c : value) {
    if (std::isalnum(c) != 0 || c == '-' || c == '_' || c == '.' || c == '~') {
      out.push_back(static_cast<char>(c));
    } else {
      char buffer[4];
      std::snprintf(buffer, sizeof buffer, "%%%02X", c);
      out += buffer;
    }
  }
  return out;
}

std::string clip(const std::string& value, std::size_t limit) {
  if (value.size() <= limit) {
    return value;
  }
  return value.substr(0, limit) + "\n[truncated: " + std::to_string(value.size() - limit) +
         " more bytes]";
}

Json run_with_deadline(const ToolHandler& handler, const Json& arguments, int timeout_ms,
                       std::string* error) {
  if (!handler) {
    if (error != nullptr) *error = "tool has no handler";
    return Json();
  }
  if (timeout_ms <= 0) {
    try {
      return handler(arguments);
    } catch (const std::exception& failure) {
      if (error != nullptr) *error = failure.what();
      return Json();
    }
  }

  // Inputs are copied so an abandoned worker never reads caller-owned state.
  auto task = std::async(std::launch::async, [handler, arguments]() { return handler(arguments); });
  if (task.wait_for(std::chrono::milliseconds(timeout_ms)) != std::future_status::ready) {
    if (error != nullptr) {
      *error = "timed out after " + std::to_string(timeout_ms) + " ms";
    }
    return Json();
  }
  try {
    return task.get();
  } catch (const std::exception& failure) {
    if (error != nullptr) *error = failure.what();
    return Json();
  }
}

}  // namespace ps5h
