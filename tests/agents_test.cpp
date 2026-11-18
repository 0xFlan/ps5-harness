// Host tests for local agent profiles: schema, overrides, prompt-file
// confinement, security-key rejection and listing.

#include <sys/stat.h>
#include <unistd.h>

#include <cassert>
#include <cstdio>
#include <cstdlib>
#include <string>

#include "ps5h/agents.h"

using namespace ps5h;

namespace {

std::string make_temp_dir() {
  char pattern[] = "/tmp/ps5h-agents-XXXXXX";
  char* made = ::mkdtemp(pattern);
  assert(made != nullptr);
  return std::string(made);
}

void write_text(const std::string& path, const std::string& text) {
  FILE* file = std::fopen(path.c_str(), "wb");
  assert(file != nullptr);
  std::fwrite(text.data(), 1, text.size(), file);
  std::fclose(file);
}

void test_name_validation() {
  assert(valid_agent_name("archivist"));
  assert(valid_agent_name("a-b_c9"));
  assert(!valid_agent_name(""));
  assert(!valid_agent_name("../escape"));
  assert(!valid_agent_name("a/b"));
  assert(!valid_agent_name(".hidden"));
  assert(!valid_agent_name(std::string(65, 'a')));
}

void test_inline_profile() {
  const std::string root = make_temp_dir();
  write_text(root + "/archivist.json",
             R"({
  "description": "Local test agent",
  "system_prompt": "LOCAL PROFILE PROMPT",
  "model": "profile-model",
  "temperature": 0.4,
  "max_tokens": 512,
  "max_steps": 5
})");

  const AgentProfileLoad loaded = load_agent_profile(root, "archivist");
  assert(loaded.ok);
  assert(loaded.profile.name == "archivist");
  assert(loaded.profile.description == "Local test agent");
  assert(loaded.profile.has_system_prompt);
  assert(loaded.profile.system_prompt == "LOCAL PROFILE PROMPT");
  assert(loaded.profile.has_model);
  assert(loaded.profile.model == "profile-model");
  assert(loaded.profile.has_temperature);
  assert(loaded.profile.temperature == 0.4);
  assert(loaded.profile.has_max_tokens);
  assert(loaded.profile.max_tokens == 512);
  assert(loaded.profile.has_max_steps);
  assert(loaded.profile.max_steps == 5);
}

void test_prompt_file() {
  const std::string root = make_temp_dir();
  assert(::mkdir((root + "/prompts").c_str(), 0700) == 0);
  write_text(root + "/prompts/archivist.md", "PROMPT FROM FILE\n");
  write_text(root + "/archivist.json",
             R"({"system_prompt_file": "prompts/archivist.md"})");

  const AgentProfileLoad loaded = load_agent_profile(root, "archivist");
  assert(loaded.ok);
  assert(loaded.profile.has_system_prompt);
  assert(loaded.profile.system_prompt == "PROMPT FROM FILE\n");
}

void test_rejects_escapes_and_security_keys() {
  const std::string root = make_temp_dir();
  const std::string base = root.substr(root.find_last_of('/') + 1);
  const std::string outside = root + "-outside.md";
  write_text(outside, "OUTSIDE PROMPT");

  write_text(root + "/outside.json",
             R"({"system_prompt_file": "../)" + base + R"(-outside.md"})");
  const AgentProfileLoad escaped = load_agent_profile(root, "outside");
  assert(!escaped.ok);
  assert(escaped.error.find("outside") != std::string::npos);

  write_text(root + "/absolute.json",
             R"({"system_prompt_file": "/etc/hosts"})");
  const AgentProfileLoad absolute = load_agent_profile(root, "absolute");
  assert(!absolute.ok);
  assert(absolute.error.find("relative") != std::string::npos);

  const std::string link = root + "/link.md";
  assert(::symlink("/etc/hosts", link.c_str()) == 0);
  write_text(root + "/linked.json",
             R"({"system_prompt_file": "link.md"})");
  const AgentProfileLoad linked = load_agent_profile(root, "linked");
  assert(!linked.ok);
  assert(linked.error.find("outside") != std::string::npos);

  write_text(root + "/roots.json",
             R"({"system_prompt": "x", "allowed_roots": ["/"]})");
  const AgentProfileLoad roots = load_agent_profile(root, "roots");
  assert(!roots.ok);
  assert(roots.error.find("allowed_roots") != std::string::npos);
}

void test_rejects_bad_values() {
  const std::string root = make_temp_dir();
  write_text(root + "/both.json",
             R"({"system_prompt": "x", "system_prompt_file": "p.md"})");
  assert(!load_agent_profile(root, "both").ok);

  write_text(root + "/hot.json",
             R"({"system_prompt": "x", "temperature": 2.5})");
  assert(!load_agent_profile(root, "hot").ok);

  write_text(root + "/steps.json",
             R"({"system_prompt": "x", "max_steps": 0})");
  assert(!load_agent_profile(root, "steps").ok);

  write_text(root + "/huge.json",
             R"({"system_prompt": ")" + std::string(70000, 'x') + R"("})");
  assert(!load_agent_profile(root, "huge").ok);

  assert(!load_agent_profile(root, "missing").ok);
}

void test_listing() {
  const std::string root = make_temp_dir();
  write_text(root + "/beta.json",
             R"({"description": "Beta agent", "system_prompt": "b"})");
  write_text(root + "/alpha.json",
             R"({"description": "Alpha agent", "system_prompt": "a"})");
  write_text(root + "/broken.json", "{");

  const auto entries = list_agent_profiles(root);
  assert(entries.size() == 3);
  assert(entries[0].name == "alpha");
  assert(entries[0].description == "Alpha agent");
  assert(entries[1].name == "beta");
  assert(entries[1].description == "Beta agent");
  assert(entries[2].name == "broken");
  assert(!entries[2].error.empty());
}

}  // namespace

int main() {
  std::setvbuf(stdout, nullptr, _IONBF, 0);

  std::printf("agent names...\n");
  test_name_validation();
  std::printf("agent profile...\n");
  test_inline_profile();
  std::printf("agent prompt file...\n");
  test_prompt_file();
  std::printf("agent confinement...\n");
  test_rejects_escapes_and_security_keys();
  std::printf("agent values...\n");
  test_rejects_bad_values();
  std::printf("agent listing...\n");
  test_listing();

  std::printf("local agent tests passed\n");
  return 0;
}
