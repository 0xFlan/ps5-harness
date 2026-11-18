// Host tests for mission parsing and its hard bounds. Execution is covered by
// the end-to-end test against the real CLI and stub provider.

#include <unistd.h>

#include <cassert>
#include <cstdio>
#include <cstdlib>
#include <string>

#include "ps5h/mission.h"

using namespace ps5h;

namespace {

std::string make_temp_file(const std::string& text) {
  char pattern[] = "/tmp/ps5h-mission-XXXXXX";
  const int fd = ::mkstemp(pattern);
  assert(fd >= 0);
  FILE* file = ::fdopen(fd, "wb");
  assert(file != nullptr);
  std::fwrite(text.data(), 1, text.size(), file);
  std::fclose(file);
  return pattern;
}

MissionLoad load_text(const std::string& text) {
  return load_mission(make_temp_file(text));
}

void test_valid_mission() {
  MissionLoad loaded = load_text(R"({
    "name": "local-map",
    "steps": [
      {"id": "map", "profile": "architect", "task": "map roots", "max_steps": 3},
      {"id": "lanes", "swarm": [
        {"profile": "scout", "task": "binaries", "max_tokens": 100},
        {"profile": "architect", "task": "mounts"}
      ]}
    ]
  })");
  assert(loaded.ok);
  assert(loaded.mission.name == "local-map");
  assert(loaded.mission.steps.size() == 2);
  assert(loaded.mission.steps[0].profile == "architect");
  assert(loaded.mission.steps[0].max_steps == 3);
  assert(loaded.mission.steps[1].swarm.size() == 2);
  assert(loaded.mission.steps[1].swarm[0].profile == "scout");
  assert(loaded.mission.steps[1].swarm[0].max_tokens == 100);
}

void test_invalid_missions() {
  assert(!load_text("{}").ok);
  assert(!load_text(R"({"steps": []})").ok);
  assert(load_text(R"({"steps": [{"profile": "x", "task": "y"}]})").ok);
  assert(!load_text(R"({"steps": [{"id": "x"}]})").ok);

  MissionLoad duplicate = load_text(R"({
    "steps": [
      {"id": "same", "profile": "x", "task": "a"},
      {"id": "same", "profile": "x", "task": "b"}
    ]
  })");
  assert(!duplicate.ok);
  assert(duplicate.error.find("duplicate") != std::string::npos);

  MissionLoad both = load_text(R"({
    "steps": [{"id": "mix", "profile": "x", "task": "a",
               "swarm": [{"profile": "y", "task": "b"}]}]
  })");
  assert(!both.ok);
  assert(both.error.find("both") != std::string::npos);

  MissionLoad empty_swarm = load_text(R"({
    "steps": [{"id": "empty", "swarm": []}]
  })");
  assert(!empty_swarm.ok);

  MissionLoad too_wide = load_text(R"({
    "steps": [{"id": "wide", "swarm": [
      {"profile": "a", "task": "1"}, {"profile": "a", "task": "2"},
      {"profile": "a", "task": "3"}, {"profile": "a", "task": "4"},
      {"profile": "a", "task": "5"}, {"profile": "a", "task": "6"},
      {"profile": "a", "task": "7"}, {"profile": "a", "task": "8"},
      {"profile": "a", "task": "9"}
    ]}]
  })");
  assert(!too_wide.ok);
  assert(too_wide.error.find("width") != std::string::npos);

  MissionLoad too_many_steps = load_text(R"({"steps": [
    1,2,3,4,5,6,7,8,9,10,11,12,13,14,15,16,17,18,19,20,21,22,23,24,25,26,
    27,28,29,30,31,32,33
  ]})");
  assert(!too_many_steps.ok);

  MissionLoad bad_budget = load_text(R"({
    "steps": [{"id": "b", "profile": "x", "task": "x", "max_steps": 65}]
  })");
  assert(!bad_budget.ok);
  assert(bad_budget.error.find("max_steps") != std::string::npos);

  MissionLoad missing_file = load_mission("/tmp/ps5h-mission-does-not-exist");
  assert(!missing_file.ok);
  assert(missing_file.error.find("cannot open") != std::string::npos);
}

}  // namespace

int main() {
  std::setvbuf(stdout, nullptr, _IONBF, 0);
  test_valid_mission();
  test_invalid_missions();
  std::printf("mission tests passed\n");
  return 0;
}
