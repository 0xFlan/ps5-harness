// Host tests for local skills: discovery, frontmatter, bundled resources and
// containment. A skill is instructions, so the important property is that its
// name and resource paths cannot escape the skills directory.

#include <sys/stat.h>
#include <unistd.h>

#include <cassert>
#include <cstdio>
#include <string>
#include <vector>

#include "ps5h/builtin_skills.h"
#include "ps5h/json.h"
#include "ps5h/skills.h"

using namespace ps5h;

namespace {

std::string make_temp_dir() {
  char pattern[] = "/tmp/ps5h-skills-XXXXXX";
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

const SkillEntry* find_entry(const std::vector<SkillEntry>& entries,
                             const std::string& name) {
  for (const SkillEntry& entry : entries) {
    if (entry.name == name) {
      return &entry;
    }
  }
  return nullptr;
}

void test_names() {
  assert(valid_skill_name("console-notes"));
  assert(valid_skill_name("a_b9"));
  assert(!valid_skill_name(""));
  assert(!valid_skill_name("../escape"));
  assert(!valid_skill_name("a/b"));
  assert(!valid_skill_name(".hidden"));
}

void test_list_and_load() {
  const std::string root = make_temp_dir();
  assert(::mkdir((root + "/console-notes").c_str(), 0700) == 0);
  assert(::mkdir((root + "/console-notes/templates").c_str(), 0700) == 0);
  write_text(root + "/console-notes/SKILL.md",
             "---\n"
             "name: console-notes\n"
             "description: Notes for the console\n"
             "license: MIT\n"
             "---\n\n"
             "# Console notes\n\n"
             "Write a note.\n");
  write_text(root + "/console-notes/templates/note.md", "NOTE TEMPLATE\n");
  write_text(root + "/console-notes/credentials.txt", "should not be listed\n");

  assert(::mkdir((root + "/broken").c_str(), 0700) == 0);
  write_text(root + "/broken/SKILL.md", "---\nname: broken\n---\n");

  assert(::mkdir((root + "/mismatch").c_str(), 0700) == 0);
  write_text(root + "/mismatch/SKILL.md",
             "---\nname: something-else\n---\n\nDo a thing.\n");

  std::string error;
  const std::vector<SkillEntry> entries = list_skills(root, &error);
  assert(error.empty());
  assert(entries.size() == 3);
  const SkillEntry* notes = find_entry(entries, "console-notes");
  assert(notes != nullptr);
  assert(notes->error.empty());
  assert(notes->description == "Notes for the console");
  const SkillEntry* broken = find_entry(entries, "broken");
  assert(broken != nullptr);
  assert(broken->error.find("no instructions") != std::string::npos);
  const SkillEntry* mismatch = find_entry(entries, "mismatch");
  assert(mismatch != nullptr);
  assert(mismatch->error.find("does not match") != std::string::npos);

  const SkillLoad loaded = load_skill(root, "console-notes", 256 * 1024);
  assert(loaded.ok);
  assert(loaded.name == "console-notes");
  assert(loaded.description == "Notes for the console");
  assert(loaded.instructions.find("# Console notes") != std::string::npos);
  assert(loaded.instructions.find("description:") == std::string::npos);
  assert(loaded.resources.size() == 1);
  assert(loaded.resources[0] == "templates/note.md");

  std::string content;
  assert(read_skill_resource(root, "console-notes", "templates/note.md",
                             4096, &content, &error));
  assert(content == "NOTE TEMPLATE\n");

  const SkillLoad missing = load_skill(root, "nope", 256 * 1024);
  assert(!missing.ok);
  assert(missing.error.find("no skill named") != std::string::npos);
}

void test_containment() {
  const std::string root = make_temp_dir();
  assert(::mkdir((root + "/console-notes").c_str(), 0700) == 0);
  write_text(root + "/console-notes/SKILL.md",
             "---\ndescription: d\n---\n\nbody\n");
  const std::string outside = root + "-outside.txt";
  write_text(outside, "OUTSIDE\n");
  assert(::symlink(outside.c_str(),
                   (root + "/console-notes/leak.txt").c_str()) == 0);
  write_text(root + "/console-notes/credentials.txt", "SECRET\n");

  std::string content;
  std::string error;
  assert(!read_skill_resource(root, "console-notes",
                              "../../console-notes/SKILL.md", 4096, &content,
                              &error));
  assert(error.find("outside the skill directory") != std::string::npos);

  error.clear();
  assert(!read_skill_resource(root, "console-notes", "leak.txt", 4096,
                              &content, &error));
  assert(error.find("outside the skill directory") != std::string::npos);

  error.clear();
  assert(!read_skill_resource(root, "console-notes", "credentials.txt", 4096,
                              &content, &error));
  assert(error.find("denied") != std::string::npos);

  error.clear();
  assert(!read_skill_resource(root, "console-notes", "/etc/hosts", 4096,
                              &content, &error));
  assert(error.find("relative") != std::string::npos);

  // A symlinked skill directory that points outside is refused as well.
  assert(::symlink(outside.c_str(), (root + "/escape").c_str()) == 0);
  error.clear();
  const SkillLoad missing = load_skill(root, "escape", 4096);
  assert(!missing.ok);
}

void test_tool() {
  const std::string root = make_temp_dir();
  assert(::mkdir((root + "/file-audit").c_str(), 0700) == 0);
  write_text(root + "/file-audit/SKILL.md",
             "---\ndescription: Audit a directory\n---\n\nAudit steps.\n");
  write_text(root + "/file-audit/template.md", "TEMPLATE\n");

  Tool tool = make_skill_tool(root, 20000);
  assert(tool.name == "skill");

  Json list_args = Json::object();
  Json listed = tool.handler(list_args);
  assert(listed["ok"].as_bool(false));
  assert(listed["skills"].size() >= 7);
  bool saw_disk = false;
  bool saw_builtin = false;
  for (const Json& entry : listed["skills"].items()) {
    if (entry["name"].as_string() == "file-audit") saw_disk = true;
    if (entry["name"].as_string() == "astrik") saw_builtin = true;
  }
  assert(saw_disk);
  assert(saw_builtin);

  Json load_args = Json::object();
  load_args["name"] = "file-audit";
  Json loaded = tool.handler(load_args);
  assert(loaded["ok"].as_bool(false));
  assert(loaded["instructions"].as_string().find("Audit steps.") !=
         std::string::npos);
  assert(loaded["resources"].size() == 1);

  Json resource_args = Json::object();
  resource_args["name"] = "file-audit";
  resource_args["resource"] = "template.md";
  Json resource = tool.handler(resource_args);
  assert(resource["ok"].as_bool(false));
  assert(resource["content"].as_string() == "TEMPLATE\n");

  Json escape_args = Json::object();
  escape_args["name"] = "file-audit";
  escape_args["resource"] = "../../file-audit/SKILL.md";
  Json escaped = tool.handler(escape_args);
  assert(!escaped["ok"].as_bool(true));

  Json orphan = Json::object();
  orphan["resource"] = "template.md";
  Json refused = tool.handler(orphan);
  assert(!refused["ok"].as_bool(true));
}

void test_builtin_packs() {
  assert(find_builtin_skill("astrik") != nullptr);
  assert(find_builtin_skill("artex") != nullptr);
  assert(find_builtin_skill("strix") != nullptr);
  assert(find_builtin_skill("cyber-strike-ai") != nullptr);
  assert(find_builtin_skill("reverse-engineer-anything") != nullptr);
  assert(find_builtin_skill("local-architect") != nullptr);
  assert(find_builtin_skill("not-a-builtin") == nullptr);
  for (const BuiltinSkill& skill : builtin_skills()) {
    assert(!skill.name.empty());
    assert(!skill.description.empty());
    assert(skill.instructions.find("no executable code") !=
           std::string::npos);
  }

  // Built-ins still list and load when no skills directory exists at all.
  Tool tool = make_skill_tool("/tmp/ps5h-skills-does-not-exist", 20000);
  Json listed = tool.handler(Json::object());
  assert(listed["ok"].as_bool(false));
  assert(listed["skills"].size() == builtin_skills().size());

  Json load_args = Json::object();
  load_args["name"] = "astrik";
  Json loaded = tool.handler(load_args);
  assert(loaded["ok"].as_bool(false));
  assert(loaded["name"].as_string() == "astrik");
  assert(loaded["instructions"].as_string().find("Evidence bar") !=
         std::string::npos);
  assert(loaded["resources"].size() == 0);

  Json resource_args = Json::object();
  resource_args["name"] = "astrik";
  resource_args["resource"] = "anything.md";
  Json resource = tool.handler(resource_args);
  assert(!resource["ok"].as_bool(true));
  assert(resource["error"].as_string().find("no bundled resources") !=
         std::string::npos);
}

void test_limits() {
  const std::string root = make_temp_dir();
  assert(::mkdir((root + "/big").c_str(), 0700) == 0);
  write_text(root + "/big/SKILL.md", std::string(200, 'x'));
  const SkillLoad loaded = load_skill(root, "big", 50);
  assert(!loaded.ok);
  assert(loaded.error.find("too large") != std::string::npos);
}

}  // namespace

int main() {
  test_names();
  test_list_and_load();
  test_containment();
  test_tool();
  test_builtin_packs();
  test_limits();
  std::printf("skills tests passed\n");
  return 0;
}
