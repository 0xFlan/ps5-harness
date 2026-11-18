#pragma once

#include <string>
#include <vector>

#include "ps5h/core.h"
#include "ps5h/delegation.h"

namespace ps5h {

struct MissionStep {
  std::string id;
  std::string profile;
  std::string task;
  int max_steps = 0;
  long long max_tokens = 0;
  std::vector<SwarmTask> swarm;
};

struct Mission {
  std::string name = "mission";
  std::vector<MissionStep> steps;
};

struct MissionLoad {
  bool ok = false;
  Mission mission;
  std::string error;
};

// Reads a bounded JSON mission file. Steps run sequentially; a step is either
// one delegated profile/task or a bounded swarm of independent lanes.
MissionLoad load_mission(const std::string& path);

// Parses already-read mission bytes. This is the production parser exposed for
// the sanitizer/fuzz harness; load_mission() only adds the file-size bound.
MissionLoad parse_mission_text(const std::string& text);

// Runs the mission against a delegator at depth 0. Each step is a child at depth
// 1, so a step itself may delegate one level further within the configured
// max_depth. Stops at the first failed step and includes prior answers as
// context, clipped so a mission cannot grow without bound.
Json run_mission(const Mission& mission, Delegator& delegator,
                 const ToolRegistry& tools, LogFn log = LogFn());

}  // namespace ps5h
