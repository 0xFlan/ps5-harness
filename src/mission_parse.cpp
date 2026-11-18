#include "ps5h/mission.h"

#include <cmath>
#include <cstdio>
#include <set>
#include <string>
#include <utility>
#include <vector>

#include "ps5h/json.h"

namespace ps5h {
namespace {

constexpr std::size_t kMaxMissionBytes = 512u * 1024u;
constexpr std::size_t kMaxSteps = 32;
constexpr std::size_t kMaxTaskBytes = 8192;

bool read_mission_file(const std::string& path, std::string* out,
                       std::string* error) {
  FILE* file = std::fopen(path.c_str(), "rb");
  if (file == nullptr) {
    *error = "cannot open mission file: " + path;
    return false;
  }
  char buffer[8192];
  std::size_t read = 0;
  while ((read = std::fread(buffer, 1, sizeof buffer, file)) > 0) {
    if (out->size() + read > kMaxMissionBytes) {
      std::fclose(file);
      *error = "mission file exceeds the 512 KiB limit";
      return false;
    }
    out->append(buffer, read);
  }
  const bool failed = std::ferror(file) != 0;
  std::fclose(file);
  if (failed) {
    *error = "cannot read mission file: " + path;
    return false;
  }
  return true;
}

bool read_budget(const Json& item, int* max_steps, long long* max_tokens,
                 std::string* error) {
  *max_steps = 0;
  *max_tokens = 0;
  if (item.contains("max_steps")) {
    if (!item["max_steps"].is_number()) {
      *error = "max_steps must be a number";
      return false;
    }
    // JSON numbers are doubles. Check finiteness and range before converting:
    // static_cast<int>(inf) or a huge finite double is undefined behaviour.
    const double steps = item["max_steps"].as_number(0);
    if (!std::isfinite(steps) || steps < 0.0 || steps > 64.0) {
      *error = "max_steps must be between 0 and 64";
      return false;
    }
    *max_steps = static_cast<int>(steps);
  }
  if (item.contains("max_tokens")) {
    if (!item["max_tokens"].is_number()) {
      *error = "max_tokens must be a number";
      return false;
    }
    const double tokens = item["max_tokens"].as_number(0);
    if (!std::isfinite(tokens) || tokens < 0.0 || tokens > 32768.0) {
      *error = "max_tokens must be between 0 and 32768";
      return false;
    }
    *max_tokens = static_cast<long long>(tokens);
  }
  return true;
}

bool read_task(const Json& item, std::string* profile, std::string* task,
               std::string* error) {
  *profile = item["profile"].as_string();
  *task = item["task"].as_string();
  if (profile->empty() || task->empty()) {
    *error = "each mission step requires profile and task";
    return false;
  }
  if (task->size() > kMaxTaskBytes) {
    *error = "mission task exceeds the 8 KiB limit";
    return false;
  }
  return true;
}

}  // namespace

MissionLoad parse_mission_text(const std::string& text) {
  MissionLoad result;
  Json root;
  try {
    root = Json::parse(text);
  } catch (const JsonError& failure) {
    result.error = std::string("invalid mission JSON: ") + failure.what();
    return result;
  }
  if (!root.is_object()) {
    result.error = "mission root must be an object";
    return result;
  }
  result.mission.name = root["name"].as_string("mission");
  if (result.mission.name.size() > 80) {
    result.mission.name.resize(80);
  }
  const Json& steps = root["steps"];
  if (!steps.is_array() || steps.size() == 0) {
    result.error = "mission needs a non-empty steps array";
    return result;
  }
  if (steps.size() > kMaxSteps) {
    result.error = "mission has too many steps (limit 32)";
    return result;
  }

  std::set<std::string> ids;
  for (std::size_t i = 0; i < steps.size(); ++i) {
    const Json& item = steps[i];
    if (!item.is_object()) {
      result.error = "each mission step must be an object";
      return result;
    }
    MissionStep step;
    step.id = item["id"].as_string("step-" + std::to_string(i + 1));
    if (step.id.empty()) {
      step.id = "step-" + std::to_string(i + 1);
    }
    if (!ids.insert(step.id).second) {
      result.error = "duplicate mission step id: " + step.id;
      return result;
    }
    if (!read_budget(item, &step.max_steps, &step.max_tokens, &result.error)) {
      result.error = "step " + step.id + ": " + result.error;
      return result;
    }

    const Json& swarm = item["swarm"];
    if (!swarm.is_null()) {
      if (item.contains("profile") || item.contains("task")) {
        result.error = "step " + step.id +
                       " cannot have both profile/task and swarm";
        return result;
      }
      if (!swarm.is_array() || swarm.size() == 0) {
        result.error = "step " + step.id + " swarm must be a non-empty array";
        return result;
      }
      if (swarm.size() > 8) {
        result.error = "step " + step.id + " swarm exceeds the width limit of 8";
        return result;
      }
      for (const Json& lane : swarm.items()) {
        if (!lane.is_object()) {
          result.error = "step " + step.id + " swarm lanes must be objects";
          return result;
        }
        SwarmTask task;
        if (!read_task(lane, &task.profile, &task.task, &result.error)) {
          result.error = "step " + step.id + ": " + result.error;
          return result;
        }
        int lane_steps = 0;
        long long lane_tokens = 0;
        if (!read_budget(lane, &lane_steps, &lane_tokens, &result.error)) {
          result.error = "step " + step.id + ": " + result.error;
          return result;
        }
        task.max_steps = lane_steps;
        task.max_tokens = lane_tokens;
        step.swarm.push_back(std::move(task));
      }
    } else {
      if (!read_task(item, &step.profile, &step.task, &result.error)) {
        result.error = "step " + step.id + ": " + result.error;
        return result;
      }
    }
    result.mission.steps.push_back(std::move(step));
  }
  result.ok = true;
  return result;
}

MissionLoad load_mission(const std::string& path) {
  MissionLoad result;
  std::string text;
  if (!read_mission_file(path, &text, &result.error)) {
    return result;
  }
  return parse_mission_text(text);
}

}  // namespace ps5h
