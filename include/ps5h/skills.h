#pragma once

#include <cstddef>
#include <string>
#include <vector>

#include "ps5h/core.h"

namespace ps5h {

// A skill is a directory under the skills root containing SKILL.md:
//
//   <skills_dir>/<name>/SKILL.md
//
// The optional frontmatter carries a description; the body is instructions for
// the model. Skills are data, not capabilities: loading one never changes the
// configured path guards, URL policy or secrets.
struct SkillEntry {
  std::string name;
  std::string description;
  std::string error;
};

struct SkillLoad {
  bool ok = false;
  std::string name;
  std::string description;
  std::string instructions;
  std::vector<std::string> resources;
  std::string error;
};

bool valid_skill_name(const std::string& name);

// Returns every directory with a valid name, sorted. A malformed skill is
// reported with its error rather than hidden from the list. On failure to open
// the directory, `error` is set and the vector is empty.
std::vector<SkillEntry> list_skills(const std::string& directory,
                                    std::string* error);

SkillLoad load_skill(const std::string& directory, const std::string& name,
                     std::size_t max_bytes);

// Reads a file bundled with a skill. `resource` is relative to the skill
// directory and canonicalised before the containment check.
bool read_skill_resource(const std::string& directory, const std::string& name,
                         const std::string& resource, std::size_t max_bytes,
                         std::string* out, std::string* error);

// Lists skills when `name` is empty. With a name it returns the instructions;
// with `resource` as well it returns one bundled file. Output is clipped to
// max_output.
Tool make_skill_tool(std::string directory, std::size_t max_output);

}  // namespace ps5h
