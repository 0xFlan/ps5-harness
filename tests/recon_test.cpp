// Host tests for the read-only recon surface. The important properties are
// bounded output, path containment, secret-name denial and useful format data.

#include <sys/stat.h>
#include <unistd.h>

#include <cassert>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <limits>
#include <string>
#include <vector>

#include "ps5h/json.h"
#include "ps5h/paths.h"
#include "ps5h/recon.h"

using namespace ps5h;

namespace {

std::string make_temp_dir() {
  char pattern[] = "/tmp/ps5h-recon-XXXXXX";
  char* made = ::mkdtemp(pattern);
  assert(made != nullptr);
  return std::string(made);
}

void write_bytes(const std::string& path, const std::string& bytes) {
  FILE* file = std::fopen(path.c_str(), "wb");
  assert(file != nullptr);
  std::fwrite(bytes.data(), 1, bytes.size(), file);
  std::fclose(file);
}

void put16(std::string* out, std::size_t offset, unsigned value) {
  (*out)[offset] = static_cast<char>(value & 0xff);
  (*out)[offset + 1] = static_cast<char>((value >> 8) & 0xff);
}

void put64(std::string* out, std::size_t offset, unsigned long long value) {
  for (int i = 0; i < 8; ++i) {
    (*out)[offset + static_cast<std::size_t>(i)] =
        static_cast<char>((value >> (8 * i)) & 0xff);
  }
}

Tool make_registry(const std::string& root, ToolRegistry* registry) {
  std::string error;
  const PathGuard guard = PathGuard::from_roots({root}, &error);
  assert(error.empty());
  ReconContext context;
  context.read_guard = guard;
  context.build_id = "test-build";
  context.title_id = "TEST";
  context.write_roots = guard.roots();
  context.read_roots = guard.roots();
  context.skills_dir = root + "/skills";
  register_recon_tools(*registry, context);
  const Tool* env = registry->find("env_info");
  assert(env != nullptr);
  return *env;
}

const Json* find_entry(const Json& tree, const std::string& path) {
  for (const Json& entry : tree["entries"].items()) {
    if (entry["path"].as_string() == path) {
      return &entry;
    }
  }
  return nullptr;
}

void test_env_and_tools(const std::string& root) {
  ToolRegistry registry;
  const Tool env = make_registry(root, &registry);
  Json info = env.handler(Json::object());
  assert(info["ok"].as_bool(false));
  assert(info["build"].as_string() == "test-build");
  assert(info["title_id"].as_string() == "TEST");
  assert(info["read_roots"].size() == 1);
  assert(info["write_roots"].size() == 1);
  assert(info["skills_dir"].as_string() == root + "/skills");
  assert(info["resources"].is_object());

  const Tool* tree = registry.find("fs_tree");
  const Tool* file = registry.find("file_info");
  const Tool* strings = registry.find("fs_strings");
  assert(tree != nullptr);
  assert(file != nullptr);
  assert(strings != nullptr);
  assert(tree->description.find("secret") != std::string::npos);
}

void test_env_mount_cap(const std::string& root) {
  std::string error;
  const PathGuard guard = PathGuard::from_roots({root}, &error);
  assert(error.empty());
  ReconContext context;
  context.read_guard = guard;
  context.build_id = "mount-cap";
  for (int i = 0; i < 70; ++i) {
    MountSpace mount;
    mount.path = "/system/mount-" + std::to_string(i);
    mount.type = "nullfs";
    context.resources.mounts.push_back(mount);
  }
  for (int i = 0; i < 10; ++i) {
    MountSpace mount;
    mount.path = "/mnt/sandbox/NPXS00000_000/lane-" + std::to_string(i);
    mount.type = "nullfs";
    context.resources.mounts.push_back(mount);
  }
  ToolRegistry registry;
  register_recon_tools(registry, context);
  const Tool* env = registry.find("env_info");
  assert(env != nullptr);
  Json info = env->handler(Json::object());
  assert(info["resources"]["mounts_total"].as_number() == 80);
  assert(info["resources"]["mounts_shown"].as_number() == 64);
  assert(info["resources"]["sandbox_mounts_omitted"].as_number() == 10);
  assert(info["resources"]["mounts_omitted"].as_number() == 6);
  assert(info["resources"]["mounts"].size() == 64);
}

void test_elf_and_strings(const std::string& root) {
  ToolRegistry registry;
  make_registry(root, &registry);
  const Tool* file = registry.find("file_info");
  const Tool* strings = registry.find("fs_strings");
  assert(file != nullptr && strings != nullptr);

  const std::string elf_path = root + "/payload.elf";
  std::string elf(64, '\0');
  elf[0] = 0x7f;
  elf[1] = 'E';
  elf[2] = 'L';
  elf[3] = 'F';
  elf[4] = 2;  // ELF64
  elf[5] = 1;  // little endian
  elf[6] = 1;  // current version
  put16(&elf, 16, 3);     // ET_DYN
  put16(&elf, 18, 0x3e);  // x86-64
  put64(&elf, 24, 0x401000);
  put64(&elf, 32, 64);
  put16(&elf, 56, 1);
  put16(&elf, 60, 2);
  write_bytes(elf_path, elf + std::string("hello world\0second string\0", 26));

  Json args = Json::object();
  args["path"] = elf_path;
  Json info = file->handler(args);
  assert(info["ok"].as_bool(false));
  assert(info["format"].as_string() == "elf");
  assert(info["elf"]["class"].as_string() == "ELF64");
  assert(info["elf"]["machine"].as_string() == "x86-64");
  assert(info["elf"]["type"].as_string() == "ET_DYN");
  assert(info["elf"]["entry"].as_string() == "0x401000");
  assert(info["sha256"].as_string().size() == 64);
  assert(info["mode"].as_string().size() == 4);

  Json string_args = Json::object();
  string_args["path"] = elf_path;
  string_args["min_length"] = 6;
  Json found = strings->handler(string_args);
  assert(found["ok"].as_bool(false));
  bool saw_hello = false;
  bool saw_second = false;
  for (const Json& text : found["strings"].items()) {
    if (text.as_string() == "hello world") saw_hello = true;
    if (text.as_string() == "second string") saw_second = true;
  }
  assert(saw_hello);
  assert(saw_second);
}

void test_hostile_headers_and_numbers(const std::string& root) {
  // Truncated containers must never read past the supplied prefix.
  for (std::size_t size = 0; size <= 64; ++size) {
    const Json header = recon_detail::describe_header(std::string(size, '\0'));
    assert(header["format"].is_string());
    assert(header["magic"].is_string());
  }

  // A PE e_lfanew near UINT32_MAX used to wrap pe_offset + 4 and make
  // string::compare throw out_of_range.
  const std::uint32_t offsets[] = {0xffffffffu, 0xfffffffeu, 0xfffffffdu,
                                   0xfffffffcu};
  for (std::uint32_t offset : offsets) {
    std::string pe(64, '\0');
    pe[0] = 'M';
    pe[1] = 'Z';
    pe[0x3c] = static_cast<char>(offset & 0xff);
    pe[0x3d] = static_cast<char>((offset >> 8) & 0xff);
    pe[0x3e] = static_cast<char>((offset >> 16) & 0xff);
    pe[0x3f] = static_cast<char>((offset >> 24) & 0xff);
    const Json header = recon_detail::describe_header(pe);
    assert(header["format"].as_string() == "pe");
    assert(header["pe"]["signature"].as_bool(true) == false);
  }

  const StringScanResult scan =
      recon_detail::scan_strings(std::string(400, 'A') + "\0" + "BCDEF", 4,
                                 1);
  assert(scan.strings.size() == 1);
  assert(scan.strings[0].size() == 160);
  assert(scan.hit_limit);

  ToolRegistry registry;
  make_registry(root, &registry);
  const Tool* tree = registry.find("fs_tree");
  const Tool* strings = registry.find("fs_strings");
  assert(tree != nullptr && strings != nullptr);

  const std::string payload = root + "/numbers.bin";
  write_bytes(payload, "\x7f" "ELF" + std::string(60, '\0') + "hello\0");

  Json tree_args = Json::object();
  tree_args["path"] = root;
  tree_args["depth"] = Json(std::numeric_limits<double>::infinity());
  Json listing = tree->handler(tree_args);
  assert(listing["ok"].as_bool(false));
  assert(listing["depth"].as_number() == 8);

  Json string_args = Json::object();
  string_args["path"] = payload;
  string_args["min_length"] = Json(std::numeric_limits<double>::infinity());
  Json found = strings->handler(string_args);
  assert(found["ok"].as_bool(false));
  assert(found["strings"].size() <= 200);
}

void test_tree_and_containment(const std::string& root) {
  const std::string tree_root = canonical_path(root);
  assert(!tree_root.empty());
  assert(::mkdir((root + "/sub").c_str(), 0700) == 0);
  write_bytes(root + "/sub/one.txt", "one\n");
  write_bytes(root + "/visible.txt", "visible\n");
  write_bytes(root + "/credentials.txt", "sha-secret\n");
  write_bytes(root + "/sub/nested.txt", "nested\n");
  const std::string outside = root + "-outside.txt";
  write_bytes(outside, "outside\n");
  assert(::symlink(outside.c_str(), (root + "/outside-link").c_str()) == 0);
  assert(::symlink((root + "/credentials.txt").c_str(),
                   (root + "/secret-link").c_str()) == 0);

  ToolRegistry registry;
  make_registry(root, &registry);
  const Tool* tree = registry.find("fs_tree");
  const Tool* file = registry.find("file_info");
  assert(tree != nullptr && file != nullptr);

  Json args = Json::object();
  args["path"] = root;
  args["depth"] = 3;
  Json listing = tree->handler(args);
  assert(listing["ok"].as_bool(false));
  assert(listing["truncated"].as_bool(true) == false);
  assert(find_entry(listing, tree_root + "/visible.txt") != nullptr);
  assert(find_entry(listing, tree_root + "/sub/one.txt") != nullptr);
  assert(find_entry(listing, tree_root + "/sub/nested.txt") != nullptr);
  const Json* secret = find_entry(listing, tree_root + "/credentials.txt");
  assert(secret != nullptr);
  assert((*secret)["type"].as_string() == "denied");
  assert(!secret->contains("size"));
  const Json* link = find_entry(listing, tree_root + "/outside-link");
  assert(link != nullptr);
  assert((*link)["type"].as_string() == "link");
  assert((*link)["target"].as_string() == outside);
  const Json* secret_link = find_entry(listing, tree_root + "/secret-link");
  assert(secret_link != nullptr);
  assert((*secret_link)["type"].as_string() == "denied");
  assert(!secret_link->contains("target"));

  Json outside_args = Json::object();
  outside_args["path"] = "/etc/hosts";
  Json refused = tree->handler(outside_args);
  assert(!refused["ok"].as_bool(true));
  assert(refused["error"].as_string().find("outside the read roots") !=
         std::string::npos);

  Json secret_args = Json::object();
  secret_args["path"] = root + "/credentials.txt";
  Json denied = file->handler(secret_args);
  assert(!denied["ok"].as_bool(true));
  assert(denied["error"].as_string().find("denied") != std::string::npos);

  // Depth 1 reports the immediate children but does not descend into sub/.
  Json shallow_args = Json::object();
  shallow_args["path"] = root;
  shallow_args["depth"] = 1;
  Json shallow = tree->handler(shallow_args);
  assert(find_entry(shallow, tree_root + "/sub") != nullptr);
  assert(find_entry(shallow, tree_root + "/sub/one.txt") == nullptr);
}

}  // namespace

int main() {
  std::setvbuf(stdout, nullptr, _IONBF, 0);
  const std::string root = make_temp_dir();
  test_env_and_tools(root);
  test_env_mount_cap(root);
  test_elf_and_strings(root);
  test_hostile_headers_and_numbers(root);
  test_tree_and_containment(root);
  std::printf("recon tests passed\n");
  return 0;
}
