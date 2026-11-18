#pragma once

#include <string>
#include <vector>

namespace ps5h {

// Built-in skills are compiled into the harness so a fresh console or host run
// has useful local-recon and reverse-engineering methodology without staging a
// directory. They are plain text: no scripts, binaries, network endpoints or
// install steps. A file under the configured skills directory with the same
// name takes precedence.
struct BuiltinSkill {
  std::string name;
  std::string description;
  std::string instructions;
};

const std::vector<BuiltinSkill>& builtin_skills();
const BuiltinSkill* find_builtin_skill(const std::string& name);

}  // namespace ps5h
