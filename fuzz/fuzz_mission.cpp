// Fuzz target: mission files drive multi-step and swarm execution. The parser
// must reject hostile JSON without crashing, reading out of bounds, or
// converting non-finite numbers to integers.

#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <string>

#include "ps5h/mission.h"

extern "C" int LLVMFuzzerTestOneInput(const std::uint8_t* data, std::size_t size) {
  std::string text;
  if (size != 0) {
    text.assign(reinterpret_cast<const char*>(data), size);
  }
  const ps5h::MissionLoad loaded = ps5h::parse_mission_text(text);

  if (!loaded.ok) {
    return 0;
  }
  if (loaded.mission.steps.empty() || loaded.mission.steps.size() > 32) {
    std::abort();
  }
  for (const ps5h::MissionStep& step : loaded.mission.steps) {
    if (step.max_steps < 0 || step.max_steps > 64 ||
        step.max_tokens < 0 || step.max_tokens > 32768) {
      std::abort();
    }
    if (step.swarm.size() > 8) {
      std::abort();
    }
    if (step.swarm.empty()) {
      if (step.profile.empty() || step.task.empty() ||
          step.task.size() > 8192) {
        std::abort();
      }
    }
    for (const ps5h::SwarmTask& lane : step.swarm) {
      if (lane.profile.empty() || lane.task.empty() || lane.task.size() > 8192 ||
          lane.max_steps < 0 || lane.max_steps > 64 || lane.max_tokens < 0 ||
          lane.max_tokens > 32768) {
        std::abort();
      }
    }
  }
  return 0;
}
