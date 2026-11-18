#include "ps5h/agents.h"

#include <dirent.h>
#include <sys/stat.h>
#include <unistd.h>

#include <algorithm>
#include <cctype>
#include <cstddef>
#include <cstdio>
#include <string>
#include <utility>
#include <vector>

#include "ps5h/json.h"
#include "ps5h/paths.h"

namespace ps5h {
namespace {

constexpr std::size_t kMaxProfileBytes = 256 * 1024;
constexpr std::size_t kMaxPromptBytes = 64 * 1024;
constexpr std::size_t kMaxDescriptionBytes = 512;
constexpr std::size_t kMaxModelBytes = 128;

bool read_text(const std::string& path, std::size_t limit, std::string* out,
               std::string* error) {
  FILE* file = std::fopen(path.c_str(), "rb");
  if (file == nullptr) {
    *error = "cannot read " + path;
    return false;
  }
  char buffer[4096];
  std::size_t read = 0;
  while ((read = std::fread(buffer, 1, sizeof buffer, file)) > 0) {
    if (out->size() + read > limit) {
      std::fclose(file);
      *error = "file is too large: " + path;
      return false;
    }
    out->append(buffer, read);
  }
  std::fclose(file);
  return true;
}

std::string resolve_directory(const std::string& directory) {
  if (directory.empty()) {
    return std::string();
  }
  if (directory.front() == '/') {
    return canonical_path(directory);
  }
  char buffer[4096];
  if (getcwd(buffer, sizeof buffer) == nullptr) {
    return std::string();
  }
  return canonical_path(std::string(buffer) + "/" + directory);
}

bool inside(const std::string& root, const std::string& path) {
  if (root == "/") {
    return !path.empty() && path.front() == '/';
  }
  if (path.size() <= root.size()) {
    return false;
  }
  return path.compare(0, root.size(), root) == 0 && path[root.size()] == '/';
}

bool integral_number(const Json& value, int minimum, int maximum, int* out,
                     const char* field, std::string* error) {
  if (!value.is_number()) {
    *error = std::string(field) + " must be a number";
    return false;
  }
  const double number = value.as_number();
  if (number < minimum || number > maximum ||
      number != static_cast<double>(static_cast<int>(number))) {
    *error = std::string(field) + " must be an integer in [" +
             std::to_string(minimum) + ", " + std::to_string(maximum) + "]";
    return false;
  }
  *out = static_cast<int>(number);
  return true;
}

}  // namespace

bool valid_agent_name(const std::string& name) {
  if (name.empty() || name.size() > 64) {
    return false;
  }
  for (unsigned char c : name) {
    if (!(std::isalnum(c) || c == '_' || c == '-')) {
      return false;
    }
  }
  return true;
}

AgentProfileLoad load_agent_profile(const std::string& directory,
                                    const std::string& name) {
  AgentProfileLoad result;
  if (!valid_agent_name(name)) {
    result.error = "invalid agent name";
    return result;
  }

  const std::string root = resolve_directory(directory);
  if (root.empty()) {
    result.error = "agents directory not found: " + directory;
    return result;
  }

  const std::string path = canonical_path(root + "/" + name + ".json");
  if (path.empty()) {
    result.error = "no agent named " + name + " in " + root;
    return result;
  }
  if (!inside(root, path)) {
    result.error = "agent file is outside the agents directory";
    return result;
  }
  if (looks_secret(path)) {
    result.error = "agent file is denied by the secret-path rules";
    return result;
  }

  std::string text;
  std::string read_error;
  if (!read_text(path, kMaxProfileBytes, &text, &read_error)) {
    result.error = read_error;
    return result;
  }

  Json root_json;
  try {
    root_json = Json::parse(text);
  } catch (const JsonError& failure) {
    result.error = std::string("invalid JSON: ") + failure.what();
    return result;
  }
  if (!root_json.is_object()) {
    result.error = "profile must be a JSON object";
    return result;
  }

  for (const char* key : {"allowed_roots", "read_roots", "url_policy",
                          "secret_env", "endpoints_dir", "plugins_dir",
                          "api_key_env"}) {
    if (root_json.contains(key)) {
      result.error = std::string("profiles cannot set ") + key;
      return result;
    }
  }

  AgentProfile profile;
  profile.name = name;

  if (root_json.contains("description")) {
    if (!root_json["description"].is_string()) {
      result.error = "description must be a string";
      return result;
    }
    profile.description = root_json["description"].as_string();
    if (profile.description.size() > kMaxDescriptionBytes) {
      result.error = "description is too long";
      return result;
    }
  }

  const bool has_inline_prompt = root_json.contains("system_prompt");
  const bool has_prompt_file = root_json.contains("system_prompt_file");
  if (has_inline_prompt && has_prompt_file) {
    result.error = "use system_prompt or system_prompt_file, not both";
    return result;
  }
  if (has_inline_prompt) {
    if (!root_json["system_prompt"].is_string()) {
      result.error = "system_prompt must be a string";
      return result;
    }
    profile.system_prompt = root_json["system_prompt"].as_string();
    profile.has_system_prompt = true;
  } else if (has_prompt_file) {
    if (!root_json["system_prompt_file"].is_string()) {
      result.error = "system_prompt_file must be a string";
      return result;
    }
    const std::string relative = root_json["system_prompt_file"].as_string();
    if (relative.empty() || relative.front() == '/') {
      result.error = "system_prompt_file must be relative to the agents directory";
      return result;
    }
    const std::string prompt_path = canonical_path(root + "/" + relative);
    if (prompt_path.empty()) {
      result.error = "prompt file not found: " + relative;
      return result;
    }
    if (!inside(root, prompt_path)) {
      result.error = "prompt file is outside the agents directory";
      return result;
    }
    if (looks_secret(prompt_path)) {
      result.error = "prompt file is denied by the secret-path rules";
      return result;
    }
    if (!read_text(prompt_path, kMaxPromptBytes, &profile.system_prompt,
                   &read_error)) {
      result.error = read_error;
      return result;
    }
    profile.has_system_prompt = true;
  }

  if (profile.has_system_prompt) {
    if (profile.system_prompt.empty()) {
      result.error = "system prompt is empty";
      return result;
    }
    if (profile.system_prompt.size() > kMaxPromptBytes) {
      result.error = "system prompt is too long";
      return result;
    }
  }

  if (root_json.contains("model")) {
    if (!root_json["model"].is_string()) {
      result.error = "model must be a string";
      return result;
    }
    profile.model = root_json["model"].as_string();
    if (profile.model.empty() || profile.model.size() > kMaxModelBytes) {
      result.error = "model must be between 1 and 128 bytes";
      return result;
    }
    profile.has_model = true;
  }

  if (root_json.contains("temperature")) {
    if (!root_json["temperature"].is_number()) {
      result.error = "temperature must be a number";
      return result;
    }
    const double temperature = root_json["temperature"].as_number();
    if (temperature < 0.0 || temperature > 2.0) {
      result.error = "temperature must be between 0 and 2";
      return result;
    }
    profile.temperature = temperature;
    profile.has_temperature = true;
  }

  if (root_json.contains("max_tokens")) {
    int value = 0;
    if (!integral_number(root_json["max_tokens"], 0, 32768, &value,
                         "max_tokens", &result.error)) {
      return result;
    }
    profile.max_tokens = value;
    profile.has_max_tokens = true;
  }

  if (root_json.contains("max_steps")) {
    int value = 0;
    if (!integral_number(root_json["max_steps"], 1, 64, &value, "max_steps",
                         &result.error)) {
      return result;
    }
    profile.max_steps = value;
    profile.has_max_steps = true;
  }

  result.ok = true;
  result.profile = std::move(profile);
  return result;
}

std::vector<AgentProfileEntry> list_agent_profiles(
    const std::string& directory) {
  std::vector<AgentProfileEntry> entries;
  const std::string root = resolve_directory(directory);
  if (root.empty()) {
    return entries;
  }

  DIR* dir = opendir(root.c_str());
  if (dir == nullptr) {
    return entries;
  }

  std::vector<std::string> names;
  while (dirent* entry = readdir(dir)) {
    const std::string file = entry->d_name;
    if (file.size() <= 5 || file.substr(file.size() - 5) != ".json") {
      continue;
    }
    const std::string name = file.substr(0, file.size() - 5);
    if (!valid_agent_name(name)) {
      continue;
    }
    struct stat info {};
    if (stat((root + "/" + file).c_str(), &info) != 0 ||
        !S_ISREG(info.st_mode)) {
      continue;
    }
    names.push_back(name);
  }
  closedir(dir);

  std::sort(names.begin(), names.end());
  for (const std::string& name : names) {
    AgentProfileLoad loaded = load_agent_profile(root, name);
    AgentProfileEntry entry;
    entry.name = name;
    if (loaded.ok) {
      entry.description = loaded.profile.description;
    } else {
      entry.error = loaded.error;
    }
    entries.push_back(std::move(entry));
  }
  return entries;
}

}  // namespace ps5h
