#include <sys/stat.h>
#include <unistd.h>

#include <cassert>
#include <cstdio>
#include <cstdlib>
#include <string>

#include "ps5h/core.h"
#include "ps5h/json.h"
#include "ps5h/paths.h"
#include "ps5h/power_tools.h"

using namespace ps5h;

namespace {

std::string make_temp_dir() {
  char pattern[] = "/tmp/ps5h-power-XXXXXX";
  char* made = ::mkdtemp(pattern);
  assert(made != nullptr);
  return std::string(made);
}

void write_text(const std::string& path, const std::string& text) {
  FILE* file = std::fopen(path.c_str(), "wb");
  assert(file != nullptr);
  const std::size_t wrote = std::fwrite(text.data(), 1, text.size(), file);
  assert(wrote == text.size());
  std::fclose(file);
}

std::string text_of(const Json& value) {
  return value.is_string() ? value.as_string() : value.dump();
}

Json call(const ToolRegistry& registry, const std::string& name,
          const Json& arguments) {
  const Tool* tool = registry.find(name);
  assert(tool != nullptr);
  return tool->handler(arguments);
}

}  // namespace

int main() {
  const std::string root = make_temp_dir();
  std::string guard_error;
  PathGuard guard = PathGuard::from_roots({root}, &guard_error);
  assert(!guard.empty());

  ToolRegistry registry;
  PowerToolContext context;
  context.read_guard = guard;
  context.write_guard = guard;
  context.max_tool_output = 20000;
  context.log = [](const std::string&) {};
  register_power_tools(registry, context);

  Json args = Json::object();
  args["path"] = root + "/notes";
  const std::string made = text_of(call(registry, "fs_mkdir", args));
  assert(made.find("notes") != std::string::npos);
  assert(::access((root + "/notes").c_str(), F_OK) == 0);

  const std::string note = root + "/notes/note.txt";
  write_text(note, "alpha\nharness needle\nomega\n");

  Json search = Json::object();
  search["path"] = root + "/notes";
  search["query"] = "needle";
  const std::string matches = text_of(call(registry, "fs_search", search));
  assert(matches.find("note.txt:2:9") != std::string::npos);

  Json dump = Json::object();
  dump["path"] = note;
  dump["length"] = 64;
  const std::string hex = text_of(call(registry, "hexdump", dump));
  assert(hex.find("alpha") != std::string::npos);
  assert(hex.find("|") != std::string::npos);

  const std::string moved = root + "/notes/moved.txt";
  Json move = Json::object();
  move["source"] = note;
  move["destination"] = moved;
  assert(text_of(call(registry, "fs_move", move)).find("moved.txt") !=
         std::string::npos);
  assert(::access(moved.c_str(), F_OK) == 0);

  Json delete_note = Json::object();
  delete_note["path"] = moved;
  assert(text_of(call(registry, "fs_delete", delete_note)).find("deleted") !=
         std::string::npos);
  assert(::access(moved.c_str(), F_OK) != 0);

  // Outside the root must be refused even though the command is well-formed.
  Json outside = Json::object();
  outside["path"] = "/tmp/ps5h-power-outside";
  const std::string refused = text_of(call(registry, "fs_mkdir", outside));
  assert(refused.find("refused") == 0);
  assert(::access("/tmp/ps5h-power-outside", F_OK) != 0);

  // Secret-looking names stay denied.
  const std::string secret = root + "/.env";
  write_text(secret, "TOKEN=not-a-real-secret\n");
  Json delete_secret = Json::object();
  delete_secret["path"] = secret;
  assert(text_of(call(registry, "fs_delete", delete_secret)).find("refused") == 0);
  assert(::access(secret.c_str(), F_OK) == 0);

  // Moving over an existing path needs explicit consent.
  write_text(root + "/a", "a");
  write_text(root + "/b", "b");
  Json move_over = Json::object();
  move_over["source"] = root + "/a";
  move_over["destination"] = root + "/b";
  assert(text_of(call(registry, "fs_move", move_over)).find("overwrite") !=
         std::string::npos);
  move_over["overwrite"] = true;
  assert(text_of(call(registry, "fs_move", move_over)).find("->") !=
         std::string::npos);

  std::printf("power_tools_test: ok (%zu tools)\n", registry.tools().size());
  return 0;
}
