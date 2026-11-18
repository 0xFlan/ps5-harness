// Fuzz target: file_info's ELF/Mach-O/PE header probing, the bounded printable
// scanner, the bounded tree walk (including symlinks and secret names), and the
// env_info mount cap. The real tool handlers are used so the test covers the
// production code path rather than a copy.

#include <sys/stat.h>
#include <unistd.h>

#include <cassert>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <limits>
#include <string>
#include <vector>

#include "ps5h/json.h"
#include "ps5h/paths.h"
#include "ps5h/recon.h"

namespace {

using namespace ps5h;

void write_bytes(const std::string& path, const std::string& bytes) {
  FILE* file = std::fopen(path.c_str(), "wb");
  if (file == nullptr) {
    std::abort();
  }
  if (!bytes.empty()) {
    std::fwrite(bytes.data(), 1, bytes.size(), file);
  }
  std::fclose(file);
}

struct Fixture {
  std::string root;
  PathGuard guard;
  ReconContext context;
  ToolRegistry registry;
};

Fixture make_fixture() {
  char pattern[] = "/tmp/ps5h-fuzz-recon-XXXXXX";
  char* made = ::mkdtemp(pattern);
  if (made == nullptr) {
    std::abort();
  }

  Fixture fixture;
  fixture.root = made;
  const std::string nested = fixture.root + "/nested";
  if (::mkdir(nested.c_str(), 0700) != 0) {
    std::abort();
  }
  write_bytes(fixture.root + "/visible.txt", "visible payload\n");
  write_bytes(fixture.root + "/credentials.txt", "must stay denied\n");
  write_bytes(nested + "/deep.txt", "nested payload\n");
  // Neither link is followed by fs_tree. One target is outside the root and
  // one target has a denied name.
  (void)::symlink("/etc/passwd", (fixture.root + "/outside-link").c_str());
  (void)::symlink((fixture.root + "/credentials.txt").c_str(),
                  (fixture.root + "/secret-link").c_str());
  (void)::symlink("..", (fixture.root + "/parent-link").c_str());

  std::string error;
  fixture.guard = PathGuard::from_roots({fixture.root}, &error);
  if (!error.empty()) {
    std::abort();
  }
  fixture.context.read_guard = fixture.guard;
  fixture.context.build_id = "fuzz-build";
  fixture.context.title_id = "FUZZ";
  fixture.context.write_roots = fixture.guard.roots();
  fixture.context.read_roots = fixture.guard.roots();
  fixture.context.skills_dir = fixture.root + "/skills";
  for (int i = 0; i < 70; ++i) {
    MountSpace mount;
    mount.path = "/system/mount-" + std::to_string(i);
    mount.type = "nullfs";
    fixture.context.resources.mounts.push_back(mount);
  }
  for (int i = 0; i < 10; ++i) {
    MountSpace mount;
    mount.path = "/mnt/sandbox/NPXS00000_000/lane-" + std::to_string(i);
    mount.type = "nullfs";
    fixture.context.resources.mounts.push_back(mount);
  }
  register_recon_tools(fixture.registry, fixture.context);
  return fixture;
}

const Fixture& fixture() {
  static const Fixture instance = make_fixture();
  return instance;
}

void put32(std::string* bytes, std::size_t offset, std::uint32_t value) {
  for (int i = 0; i < 4; ++i) {
    (*bytes)[offset + static_cast<std::size_t>(i)] =
        static_cast<char>((value >> (8 * i)) & 0xff);
  }
}

void run_crafted_headers() {
  // Every truncated prefix of each container must be accepted without reading
  // past the supplied bytes.
  for (std::size_t size = 0; size <= 64; ++size) {
    (void)recon_detail::describe_header(std::string(size, '\0'));
  }

  std::string elf(64, '\0');
  elf[0] = 0x7f;
  elf[1] = 'E';
  elf[2] = 'L';
  elf[3] = 'F';
  elf[4] = 2;
  elf[5] = 1;
  for (std::size_t size = 0; size <= elf.size(); ++size) {
    (void)recon_detail::describe_header(elf.substr(0, size));
  }

  std::string macho(64, '\0');
  macho[0] = static_cast<char>(0xcf);
  macho[1] = static_cast<char>(0xfa);
  macho[2] = static_cast<char>(0xed);
  macho[3] = static_cast<char>(0xfe);
  for (std::size_t size = 0; size <= macho.size(); ++size) {
    (void)recon_detail::describe_header(macho.substr(0, size));
  }

  // PE e_lfanew values chosen to make pe_offset + 4 wrap in 32-bit arithmetic.
  // The bounds check must not turn these into a string::compare exception.
  const std::uint32_t offsets[] = {0xffffffffu, 0xfffffffeu, 0xfffffffdu,
                                   0xfffffffcu, 60u,         64u,
                                   0x40u,        0u};
  for (std::uint32_t offset : offsets) {
    std::string pe(64, '\0');
    pe[0] = 'M';
    pe[1] = 'Z';
    put32(&pe, 0x3c, offset);
    (void)recon_detail::describe_header(pe);
  }
}

const bool kCrafted = [] {
  run_crafted_headers();
  return true;
}();

}  // namespace

extern "C" int LLVMFuzzerTestOneInput(const std::uint8_t* data, std::size_t size) {
  (void)kCrafted;
  const std::string bytes(reinterpret_cast<const char*>(data), size);

  const Json header = recon_detail::describe_header(bytes);
  if (!header["format"].is_string() || !header["magic"].is_string()) {
    std::abort();
  }

  const int min_length = size == 0 ? 4 : static_cast<int>(data[0] % 70);
  const StringScanResult scan =
      recon_detail::scan_strings(bytes, min_length, 200);
  if (scan.strings.size() > 200) {
    std::abort();
  }
  for (const std::string& text : scan.strings) {
    if (text.size() > 160) {
      std::abort();
    }
  }

  const Fixture& f = fixture();
  write_bytes(f.root + "/payload.bin", bytes);

  Json file_args = Json::object();
  file_args["path"] = f.root + "/payload.bin";
  const Tool* file = f.registry.find("file_info");
  if (file == nullptr) {
    std::abort();
  }
  const Json info = file->handler(file_args);
  if (!info["ok"].as_bool(false) || !info["format"].is_string()) {
    std::abort();
  }

  // Alternate between a normal and a non-finite numeric argument so the range
  // checks are exercised on every run.
  Json string_args = Json::object();
  string_args["path"] = f.root + "/payload.bin";
  string_args["min_length"] =
      (size > 1 && (data[1] & 1) != 0)
          ? Json(std::numeric_limits<double>::infinity())
          : Json(size > 1 ? data[1] : 4);
  const Tool* strings = f.registry.find("fs_strings");
  if (strings == nullptr) {
    std::abort();
  }
  const Json found = strings->handler(string_args);
  if (!found["ok"].as_bool(false) || found["strings"].size() > 200) {
    std::abort();
  }
  for (const Json& text : found["strings"].items()) {
    if (text.as_string().size() > 160) {
      std::abort();
    }
  }

  Json tree_args = Json::object();
  tree_args["path"] = f.root;
  tree_args["depth"] =
      (size > 2 && (data[2] & 1) != 0)
          ? Json(std::numeric_limits<double>::infinity())
          : Json(size > 2 ? data[2] % 12 : 3);
  const Tool* tree = f.registry.find("fs_tree");
  if (tree == nullptr) {
    std::abort();
  }
  const Json listing = tree->handler(tree_args);
  if (!listing["ok"].as_bool(false) || listing["entries"].size() > 500) {
    std::abort();
  }

  const Tool* env = f.registry.find("env_info");
  if (env == nullptr) {
    std::abort();
  }
  const Json environment = env->handler(Json::object());
  if (!environment["ok"].as_bool(false) ||
      environment["resources"]["mounts_shown"].as_number() > 64) {
    std::abort();
  }
  if (environment["resources"]["sandbox_mounts_omitted"].as_number() != 10) {
    std::abort();
  }
  return 0;
}
