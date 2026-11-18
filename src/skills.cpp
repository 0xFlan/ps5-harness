#include "ps5h/skills.h"

#include <dirent.h>
#include <sys/stat.h>
#include <unistd.h>

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <utility>

#include "ps5h/json.h"
#include "ps5h/paths.h"

namespace ps5h {
namespace {

constexpr std::size_t kMaxSkillBytes = 256 * 1024;
constexpr std::size_t kMaxResources = 64;
constexpr int kMaxResourceDepth = 2;

std::string trim(const std::string& value) {
  std::size_t begin = 0;
  std::size_t end = value.size();
  while (begin < end &&
         std::isspace(static_cast<unsigned char>(value[begin])) != 0) {
    ++begin;
  }
  while (end > begin &&
         std::isspace(static_cast<unsigned char>(value[end - 1])) != 0) {
    --end;
  }
  return value.substr(begin, end - begin);
}

std::string unquote(const std::string& value) {
  if (value.size() >= 2 && ((value.front() == '"' && value.back() == '"') ||
                            (value.front() == '\'' && value.back() == '\''))) {
    return value.substr(1, value.size() - 2);
  }
  return value;
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
  const bool failed = std::ferror(file) != 0;
  std::fclose(file);
  if (failed) {
    *error = "cannot read " + path;
    return false;
  }
  return true;
}

// Splits optional leading frontmatter from the instruction body. Unknown
// frontmatter keys are ignored so a skill can carry licence or metadata fields
// without a new parser version. `name` must match the directory when present.
bool parse_skill(const std::string& text, const std::string& fallback_name,
                 std::string* description, std::string* body,
                 std::string* error) {
  std::string remaining = text;
  if (remaining.size() >= 3 &&
      remaining.compare(0, 3, "\xEF\xBB\xBF") == 0) {
    remaining.erase(0, 3);
  }

  std::string declared_name;
  std::string declared_description;

  if (remaining.rfind("---", 0) == 0) {
    const std::size_t first_end = remaining.find('\n');
    if (first_end != std::string::npos &&
        trim(remaining.substr(0, first_end)) == "---") {
      std::size_t position = first_end + 1;
      bool closed = false;
      while (position <= remaining.size()) {
        const std::size_t line_end = remaining.find('\n', position);
        const std::string line =
            remaining.substr(position, line_end == std::string::npos
                                          ? std::string::npos
                                          : line_end - position);
        if (trim(line) == "---") {
          remaining = line_end == std::string::npos
                          ? std::string()
                          : remaining.substr(line_end + 1);
          closed = true;
          break;
        }
        const std::size_t colon = line.find(':');
        if (colon != std::string::npos) {
          const std::string key = trim(line.substr(0, colon));
          const std::string value = unquote(trim(line.substr(colon + 1)));
          if (key == "name") {
            declared_name = value;
          } else if (key == "description") {
            declared_description = value;
          }
        }
        if (line_end == std::string::npos) {
          break;
        }
        position = line_end + 1;
      }
      if (!closed) {
        *error = "unterminated SKILL.md frontmatter";
        return false;
      }
    }
  }

  if (!declared_name.empty()) {
    if (!valid_skill_name(declared_name)) {
      *error = "invalid skill name in SKILL.md";
      return false;
    }
    if (declared_name != fallback_name) {
      *error = "SKILL.md name '" + declared_name +
               "' does not match its directory '" + fallback_name + "'";
      return false;
    }
  }

  std::string trimmed = trim(remaining);
  if (trimmed.empty()) {
    *error = "SKILL.md has no instructions";
    return false;
  }

  if (declared_description.empty()) {
    // A short first line makes listing useful even without frontmatter.
    std::size_t line_end = trimmed.find('\n');
    std::string first = trim(trimmed.substr(0, line_end));
    while (!first.empty() && first.front() == '#') {
      first.erase(0, 1);
      first = trim(first);
    }
    if (!first.empty()) {
      declared_description = clip(first, 200);
    }
  }

  *description = declared_description;
  *body = std::move(trimmed);
  return true;
}

void collect_resources(const std::string& skill_root,
                       const std::string& relative, int depth,
                       std::vector<std::string>* out) {
  const std::string directory =
      relative.empty() ? skill_root : skill_root + "/" + relative;
  DIR* dir = opendir(directory.c_str());
  if (dir == nullptr) {
    return;
  }
  while (dirent* entry = readdir(dir)) {
    if (out->size() >= kMaxResources) {
      break;
    }
    const std::string name = entry->d_name;
    if (name == "." || name == "..") {
      continue;
    }
    const std::string child =
        relative.empty() ? name : relative + "/" + name;
    const std::string full = skill_root + "/" + child;
    if (looks_secret(full)) {
      continue;
    }
    struct stat info {};
    if (stat(full.c_str(), &info) != 0) {
      continue;
    }
    if (S_ISDIR(info.st_mode)) {
      if (depth < kMaxResourceDepth) {
        collect_resources(skill_root, child, depth + 1, out);
      }
    } else if (S_ISREG(info.st_mode) && child != "SKILL.md") {
      out->push_back(child);
    }
  }
  closedir(dir);
}

}  // namespace

bool valid_skill_name(const std::string& name) {
  if (name.empty() || name.size() > 64) {
    return false;
  }
  for (unsigned char c : name) {
    if ((std::isalnum(c) == 0) && c != '_' && c != '-') {
      return false;
    }
  }
  return true;
}

std::vector<SkillEntry> list_skills(const std::string& directory,
                                    std::string* error) {
  std::vector<SkillEntry> entries;
  const std::string root = resolve_directory(directory);
  if (root.empty()) {
    if (error != nullptr) {
      *error = "skills directory not found: " + directory;
    }
    return entries;
  }
  DIR* dir = opendir(root.c_str());
  if (dir == nullptr) {
    if (error != nullptr) {
      *error = "cannot open skills directory: " + root;
    }
    return entries;
  }

  std::vector<std::string> names;
  while (dirent* entry = readdir(dir)) {
    const std::string name = entry->d_name;
    if (!valid_skill_name(name)) {
      continue;
    }
    struct stat info {};
    if (stat((root + "/" + name).c_str(), &info) != 0 ||
        !S_ISDIR(info.st_mode)) {
      continue;
    }
    names.push_back(name);
  }
  closedir(dir);
  std::sort(names.begin(), names.end());

  for (const std::string& name : names) {
    const SkillLoad loaded = load_skill(root, name, kMaxSkillBytes);
    SkillEntry entry;
    entry.name = name;
    if (loaded.ok) {
      entry.description = loaded.description;
    } else {
      entry.error = loaded.error;
    }
    entries.push_back(std::move(entry));
  }
  return entries;
}

SkillLoad load_skill(const std::string& directory, const std::string& name,
                     std::size_t max_bytes) {
  SkillLoad result;
  result.name = name;
  if (!valid_skill_name(name)) {
    result.error = "invalid skill name";
    return result;
  }
  const std::string root = resolve_directory(directory);
  if (root.empty()) {
    result.error = "skills directory not found: " + directory;
    return result;
  }
  const std::string path = canonical_path(root + "/" + name + "/SKILL.md");
  if (path.empty()) {
    result.error = "no skill named " + name + " in " + root;
    return result;
  }
  if (!inside(root, path)) {
    result.error = "skill file is outside the skills directory";
    return result;
  }
  if (looks_secret(path)) {
    result.error = "skill file is denied by the secret-path rules";
    return result;
  }
  struct stat info {};
  if (stat(path.c_str(), &info) != 0 || !S_ISREG(info.st_mode)) {
    result.error = "no skill named " + name + " in " + root;
    return result;
  }
  if (max_bytes > kMaxSkillBytes) {
    max_bytes = kMaxSkillBytes;
  }

  std::string text;
  if (!read_text(path, max_bytes, &text, &result.error)) {
    return result;
  }

  std::string description;
  std::string body;
  if (!parse_skill(text, name, &description, &body, &result.error)) {
    return result;
  }

  const std::string skill_root = root + "/" + name;
  collect_resources(skill_root, "", 0, &result.resources);
  std::sort(result.resources.begin(), result.resources.end());

  result.ok = true;
  result.description = description;
  result.instructions = std::move(body);
  return result;
}

bool read_skill_resource(const std::string& directory, const std::string& name,
                         const std::string& resource, std::size_t max_bytes,
                         std::string* out, std::string* error) {
  if (!valid_skill_name(name)) {
    *error = "invalid skill name";
    return false;
  }
  if (resource.empty() || resource.front() == '/') {
    *error = "resource must be relative to the skill directory";
    return false;
  }
  const std::string root = resolve_directory(directory);
  if (root.empty()) {
    *error = "skills directory not found: " + directory;
    return false;
  }
  const std::string skill_root = canonical_path(root + "/" + name);
  if (skill_root.empty()) {
    *error = "no skill named " + name;
    return false;
  }
  if (!inside(root, skill_root)) {
    *error = "skill directory is outside the skills directory";
    return false;
  }
  const std::string path = canonical_path(skill_root + "/" + resource);
  if (path.empty()) {
    *error = "resource not found: " + resource;
    return false;
  }
  if (!inside(skill_root, path)) {
    *error = "resource is outside the skill directory";
    return false;
  }
  if (looks_secret(path)) {
    *error = "resource is denied by the secret-path rules";
    return false;
  }
  struct stat info {};
  if (stat(path.c_str(), &info) != 0 || !S_ISREG(info.st_mode)) {
    *error = "resource is not a regular file: " + resource;
    return false;
  }

  std::string text;
  if (!read_text(path, max_bytes, &text, error)) {
    return false;
  }
  *out = std::move(text);
  return true;
}

Tool make_skill_tool(std::string directory, std::size_t max_output) {
  const std::string root = resolve_directory(directory);
  const std::string configured =
      root.empty() ? std::move(directory) : root;

  Tool tool;
  tool.name = "skill";
  tool.description =
      "List local skills, or load one skill's instructions by name. With a "
      "name and resource, read a file bundled with that skill. Skills are "
      "instructions only; they cannot widen the configured roots, URL policy, "
      "secrets or delegation limits.";
  Json properties = Json::object();
  properties["name"] = Json::object();
  properties["name"]["type"] = "string";
  properties["name"]["description"] =
      "Skill directory name; omit to list available skills";
  properties["resource"] = Json::object();
  properties["resource"]["type"] = "string";
  properties["resource"]["description"] =
      "Optional file path relative to the skill directory";
  tool.parameters = Json::object();
  tool.parameters["type"] = "object";
  tool.parameters["properties"] = properties;
  tool.parameters["required"] = Json::array();

  tool.handler = [configured, max_output](const Json& arguments) -> Json {
    const std::string name = arguments["name"].as_string();
    const std::string resource = arguments["resource"].as_string();

    if (name.empty()) {
      Json out = Json::object();
      if (!resource.empty()) {
        out["ok"] = false;
        out["error"] = "skill requires a name when resource is set";
        return out;
      }
      std::string error;
      const std::vector<SkillEntry> entries = list_skills(configured, &error);
      if (!error.empty()) {
        out["ok"] = false;
        out["error"] = error;
        return out;
      }
      Json skills = Json::array();
      for (const SkillEntry& entry : entries) {
        Json item = Json::object();
        item["name"] = entry.name;
        if (entry.error.empty()) {
          item["description"] = entry.description;
        } else {
          item["error"] = entry.error;
        }
        skills.push_back(std::move(item));
      }
      out["ok"] = true;
      out["skills"] = std::move(skills);
      return out;
    }

    if (!resource.empty()) {
      Json out = Json::object();
      std::string content;
      std::string error;
      if (!read_skill_resource(configured, name, resource, 64 * 1024, &content,
                               &error)) {
        out["ok"] = false;
        out["error"] = error;
        return out;
      }
      out["ok"] = true;
      out["name"] = name;
      out["resource"] = resource;
      out["content"] = clip(content, max_output);
      out["clipped"] = content.size() > max_output;
      return out;
    }

    Json out = Json::object();
    const SkillLoad loaded =
        load_skill(configured, name, kMaxSkillBytes);
    if (!loaded.ok) {
      out["ok"] = false;
      out["error"] = loaded.error;
      return out;
    }
    out["ok"] = true;
    out["name"] = loaded.name;
    out["description"] = loaded.description;
    out["instructions"] = clip(loaded.instructions, max_output);
    out["clipped"] = loaded.instructions.size() > max_output;
    Json resources = Json::array();
    for (const std::string& entry : loaded.resources) {
      resources.push_back(entry);
    }
    out["resources"] = std::move(resources);
    return out;
  };
  return tool;
}

}  // namespace ps5h
