#pragma once

#include <string>
#include <vector>

namespace ps5h {

// A local agent profile customises the prompt and model budget for one run.
// It deliberately cannot change roots, URL policy, secrets, or extension
// directories: those remain in the main config so a profile cannot widen its
// own permissions.
struct AgentProfile {
  std::string name;
  std::string description;
  std::string system_prompt;
  std::string model;
  double temperature = 0.2;
  int max_tokens = 0;
  int max_steps = 0;
  bool has_system_prompt = false;
  bool has_model = false;
  bool has_temperature = false;
  bool has_max_tokens = false;
  bool has_max_steps = false;
};

struct AgentProfileLoad {
  bool ok = false;
  AgentProfile profile;
  std::string error;
};

struct AgentProfileEntry {
  std::string name;
  std::string description;
  std::string error;
};

// Names map directly to <name>.json, so only a single path-safe segment is
// accepted.
bool valid_agent_name(const std::string& name);

AgentProfileLoad load_agent_profile(const std::string& directory,
                                    const std::string& name);

// Returns every validly named .json entry, sorted by name. A malformed profile
// is returned with its error instead of hiding it from the list.
std::vector<AgentProfileEntry> list_agent_profiles(const std::string& directory);

}  // namespace ps5h
