#include "ps5h/recon.h"

#include <dirent.h>
#include <sys/stat.h>
#include <unistd.h>

#include <algorithm>
#include <climits>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>
#include <utility>
#include <vector>

#include "ps5h/json.h"
#include "ps5h/sha256.h"

namespace ps5h {
namespace {

constexpr std::size_t kMaxTreeEntries = 500;
constexpr std::size_t kMaxHashBytes = 64u * 1024u * 1024u;
constexpr std::size_t kMaxStringsInput = 16u * 1024u * 1024u;
constexpr std::size_t kMaxStrings = 200;

std::string hex_bytes(const std::string& bytes, std::size_t limit) {
  static const char* kHex = "0123456789abcdef";
  std::string out;
  const std::size_t count = std::min(bytes.size(), limit);
  out.reserve(count * 2);
  for (std::size_t i = 0; i < count; ++i) {
    const unsigned char byte = static_cast<unsigned char>(bytes[i]);
    out.push_back(kHex[byte >> 4]);
    out.push_back(kHex[byte & 0x0f]);
  }
  return out;
}

bool read_prefix(const std::string& path, std::size_t limit, std::string* out) {
  FILE* file = std::fopen(path.c_str(), "rb");
  if (file == nullptr) {
    return false;
  }
  out->resize(limit);
  const std::size_t read = std::fread(out->data(), 1, limit, file);
  std::fclose(file);
  out->resize(read);
  return true;
}

bool read_capped(const std::string& path, std::size_t limit, std::string* out,
                 bool* truncated) {
  *truncated = false;
  FILE* file = std::fopen(path.c_str(), "rb");
  if (file == nullptr) {
    return false;
  }
  char buffer[8192];
  std::size_t read = 0;
  while ((read = std::fread(buffer, 1, sizeof buffer, file)) > 0) {
    if (out->size() + read > limit) {
      const std::size_t room = limit - out->size();
      out->append(buffer, room);
      *truncated = true;
      break;
    }
    out->append(buffer, read);
  }
  std::fclose(file);
  return true;
}

std::uint64_t read_uint(const std::string& data, std::size_t offset,
                        int width, bool little) {
  if (offset + static_cast<std::size_t>(width) > data.size()) {
    return 0;
  }
  std::uint64_t value = 0;
  if (little) {
    for (int i = width - 1; i >= 0; --i) {
      value = (value << 8) |
              static_cast<unsigned char>(data[offset + static_cast<std::size_t>(i)]);
    }
  } else {
    for (int i = 0; i < width; ++i) {
      value = (value << 8) |
              static_cast<unsigned char>(data[offset + static_cast<std::size_t>(i)]);
    }
  }
  return value;
}

std::string elf_type(std::uint64_t value) {
  switch (value) {
    case 1: return "ET_REL";
    case 2: return "ET_EXEC";
    case 3: return "ET_DYN";
    case 4: return "ET_CORE";
    default: return "ET_" + std::to_string(value);
  }
}

std::string elf_machine(std::uint64_t value) {
  switch (value) {
    case 3: return "x86";
    case 8: return "mips";
    case 40: return "arm";
    case 62: return "x86-64";
    case 183: return "aarch64";
    case 243: return "riscv";
    default: return "machine_" + std::to_string(value);
  }
}

std::string macho_cpu(std::uint32_t value) {
  switch (value) {
    case 7: return "x86";
    case 0x01000007: return "x86-64";
    case 12: return "arm";
    case 0x0100000c: return "arm64";
    default: return "cpu_" + std::to_string(value);
  }
}

std::string format_name(const std::string& prefix) {
  if (prefix.size() >= 4 && prefix.compare(0, 4, "\x7f" "ELF") == 0) {
    return "elf";
  }
  if (prefix.size() >= 4) {
    const std::uint32_t magic =
        static_cast<std::uint32_t>(static_cast<unsigned char>(prefix[0])) |
        (static_cast<std::uint32_t>(static_cast<unsigned char>(prefix[1])) << 8) |
        (static_cast<std::uint32_t>(static_cast<unsigned char>(prefix[2])) << 16) |
        (static_cast<std::uint32_t>(static_cast<unsigned char>(prefix[3])) << 24);
    if (magic == 0xfeedfaceu || magic == 0xcefaedfeu ||
        magic == 0xfeedfacfu || magic == 0xcffaedfeu) {
      return "mach-o";
    }
  }
  if (prefix.size() >= 2 && prefix[0] == 'M' && prefix[1] == 'Z') {
    return "pe";
  }
  if (prefix.size() >= 4 && prefix.compare(0, 4, "PK\x03\x04") == 0) {
    return "zip";
  }
  if (prefix.size() >= 2 && static_cast<unsigned char>(prefix[0]) == 0x1f &&
      static_cast<unsigned char>(prefix[1]) == 0x8b) {
    return "gzip";
  }
  if (prefix.size() >= 4 && prefix.compare(0, 4, "\x89PNG") == 0) {
    return "png";
  }
  if (prefix.size() >= 3 && static_cast<unsigned char>(prefix[0]) == 0xff &&
      static_cast<unsigned char>(prefix[1]) == 0xd8 &&
      static_cast<unsigned char>(prefix[2]) == 0xff) {
    return "jpeg";
  }
  if (prefix.size() >= 4 && prefix.compare(0, 4, "%PDF") == 0) {
    return "pdf";
  }
  if (prefix.size() >= 2 && prefix[0] == '#' && prefix[1] == '!') {
    return "script";
  }
  return "unknown";
}

void add_elf_info(const std::string& header, Json* out) {
  if (header.size() < 20 || header.compare(0, 4, "\x7f" "ELF") != 0) {
    return;
  }
  const bool is_64 = static_cast<unsigned char>(header[4]) == 2;
  const bool little = static_cast<unsigned char>(header[5]) != 2;
  Json elf = Json::object();
  elf["class"] = is_64 ? "ELF64" : "ELF32";
  elf["endian"] = little ? "little" : "big";
  const std::uint64_t type = read_uint(header, 16, 2, little);
  const std::uint64_t machine = read_uint(header, 18, 2, little);
  elf["type"] = elf_type(type);
  elf["machine"] = elf_machine(machine);
  const std::size_t entry_offset = 24;
  const int entry_width = is_64 ? 8 : 4;
  const std::uint64_t entry =
      read_uint(header, entry_offset, entry_width, little);
  char entry_text[32];
  std::snprintf(entry_text, sizeof entry_text, "0x%llx",
                static_cast<unsigned long long>(entry));
  elf["entry"] = entry_text;
  elf["phoff"] = static_cast<long long>(
      read_uint(header, is_64 ? 32 : 28, is_64 ? 8 : 4, little));
  elf["shoff"] = static_cast<long long>(
      read_uint(header, is_64 ? 40 : 32, is_64 ? 8 : 4, little));
  elf["phnum"] = static_cast<long long>(
      read_uint(header, is_64 ? 56 : 44, 2, little));
  elf["shnum"] = static_cast<long long>(
      read_uint(header, is_64 ? 60 : 48, 2, little));
  (*out)["elf"] = std::move(elf);
}

void add_macho_info(const std::string& header, Json* out) {
  if (header.size() < 16) {
    return;
  }
  const unsigned char b0 = static_cast<unsigned char>(header[0]);
  const unsigned char b3 = static_cast<unsigned char>(header[3]);
  const bool little = b0 == 0xce || b0 == 0xcf;
  const bool is_64 = b0 == 0xcf || b3 == 0xcf;
  Json macho = Json::object();
  macho["class"] = is_64 ? "Mach-O 64" : "Mach-O 32";
  macho["endian"] = little ? "little" : "big";
  const std::uint32_t cpu =
      static_cast<std::uint32_t>(read_uint(header, 4, 4, little));
  const std::uint32_t filetype =
      static_cast<std::uint32_t>(read_uint(header, 12, 4, little));
  macho["cpu"] = macho_cpu(cpu);
  macho["filetype"] = static_cast<long long>(filetype);
  (*out)["mach_o"] = std::move(macho);
}

void add_pe_info(const std::string& header, Json* out) {
  if (header.size() < 0x40) {
    return;
  }
  const std::uint32_t pe_offset =
      static_cast<std::uint32_t>(read_uint(header, 0x3c, 4, true));
  Json pe = Json::object();
  pe["pe_offset"] = static_cast<long long>(pe_offset);
  if (pe_offset + 4 <= header.size() &&
      header.compare(pe_offset, 4, "PE\0\0") == 0) {
    pe["signature"] = true;
    const std::uint32_t machine =
        static_cast<std::uint32_t>(read_uint(header, pe_offset + 4, 2, true));
    pe["machine"] = "0x" + hex_bytes(std::string(
                         reinterpret_cast<const char*>(&machine), 2), 2);
    pe["optional_header"] = static_cast<long long>(
        read_uint(header, pe_offset + 20, 2, true));
  } else {
    pe["signature"] = false;
  }
  (*out)["pe"] = std::move(pe);
}

std::string join_path(const std::string& directory, const std::string& name) {
  if (directory.empty() || directory.back() == '/') {
    return directory + name;
  }
  return directory + "/" + name;
}

void walk_tree(const std::string& directory, int depth, int max_depth,
               std::size_t* remaining, std::vector<Json>* entries,
               bool* truncated) {
  if (depth > max_depth || *remaining == 0) {
    *truncated = true;
    return;
  }
  DIR* dir = opendir(directory.c_str());
  if (dir == nullptr) {
    return;
  }
  while (dirent* entry = readdir(dir)) {
    if (*remaining == 0) {
      *truncated = true;
      break;
    }
    const std::string name = entry->d_name;
    if (name == "." || name == "..") {
      continue;
    }
    const std::string full = join_path(directory, name);
    Json item = Json::object();
    item["path"] = full;
    if (looks_secret(full)) {
      item["type"] = "denied";
      entries->push_back(std::move(item));
      --*remaining;
      continue;
    }
    struct stat info {};
    if (lstat(full.c_str(), &info) != 0) {
      continue;
    }
    if (S_ISLNK(info.st_mode)) {
      item["type"] = "link";
      char target[PATH_MAX];
      const ssize_t length = readlink(full.c_str(), target, sizeof target - 1);
      if (length > 0) {
        target[length] = '\0';
        if (looks_secret(target)) {
          item["type"] = "denied";
        } else {
          item["target"] = target;
        }
      }
    } else if (S_ISDIR(info.st_mode)) {
      item["type"] = "dir";
      item["size"] = static_cast<long long>(info.st_size);
    } else if (S_ISREG(info.st_mode)) {
      item["type"] = "file";
      item["size"] = static_cast<long long>(info.st_size);
    } else {
      item["type"] = "other";
      item["size"] = static_cast<long long>(info.st_size);
    }
    entries->push_back(std::move(item));
    --*remaining;
    if (S_ISDIR(info.st_mode) && depth < max_depth) {
      walk_tree(full, depth + 1, max_depth, remaining, entries, truncated);
    }
  }
  closedir(dir);
}

Json tool_error(const std::string& error) {
  Json out = Json::object();
  out["ok"] = false;
  out["error"] = error;
  return out;
}

bool allowed_path(const PathGuard& guard, const std::string& requested,
                  const std::string& operation, std::string* canonical,
                  Json* error) {
  if (!guard.allows(requested)) {
    *error = tool_error(operation + " path is outside the read roots");
    return false;
  }
  const std::string path = canonical_path(requested);
  if (path.empty()) {
    *error = tool_error("cannot resolve " + requested);
    return false;
  }
  if (looks_secret(path)) {
    *error = tool_error("path is denied (configuration, credentials or state)");
    return false;
  }
  *canonical = path;
  return true;
}

}  // namespace

void register_recon_tools(ToolRegistry& registry, const ReconContext& context) {
  {
    Tool tool;
    tool.name = "env_info";
    tool.description =
        "Report the harness build, runtime budget, mounts, read/write roots, "
        "skills directory and platform. Read-only; no secrets.";
    tool.parameters = Json::object();
    tool.parameters["type"] = "object";
    tool.parameters["properties"] = Json::object();
    tool.handler = [context](const Json&) -> Json {
      Json out = Json::object();
      out["ok"] = true;
      out["build"] = context.build_id;
      out["title_id"] = context.title_id;
#if defined(PS5H_PS5_TARGET)
      out["platform"] = "ps5";
#elif defined(__APPLE__)
      out["platform"] = "macos";
#elif defined(__linux__)
      out["platform"] = "linux";
#else
      out["platform"] = "unknown";
#endif
      char cwd[PATH_MAX];
      if (getcwd(cwd, sizeof cwd) != nullptr) {
        out["cwd"] = cwd;
      }

      Json resources = Json::object();
      resources["cpu_online"] = context.resources.cpu_online;
      resources["cpu_configured"] = context.resources.cpu_configured;
      resources["page_bytes"] = static_cast<long long>(
          context.resources.page_bytes);
      resources["memory_total_bytes"] = static_cast<long long>(
          context.resources.memory_total_bytes);
      resources["memory_available_bytes"] = static_cast<long long>(
          context.resources.memory_available_bytes);
      resources["memory_available_known"] =
          context.resources.memory_available_known;
      // Other titles' private sandboxes are noise for a local map and can
      // expose paths the agent has no business enumerating. Keep system mounts
      // and candidates, cap the list, and report what was omitted.
      Json mounts = Json::array();
      std::size_t sandbox_omitted = 0;
      std::size_t cap_omitted = 0;
      for (const MountSpace& mount : context.resources.mounts) {
        if (mount.path.rfind("/mnt/sandbox/", 0) == 0) {
          ++sandbox_omitted;
          continue;
        }
        if (mounts.size() >= 64) {
          ++cap_omitted;
          continue;
        }
        Json item = Json::object();
        item["path"] = mount.path;
        item["type"] = mount.type;
        item["free_bytes"] = static_cast<long long>(mount.free_bytes);
        item["total_bytes"] = static_cast<long long>(mount.total_bytes);
        mounts.push_back(std::move(item));
      }
      resources["mounts_total"] = static_cast<long long>(
          context.resources.mounts.size());
      resources["mounts_shown"] = static_cast<long long>(mounts.size());
      resources["sandbox_mounts_omitted"] =
          static_cast<long long>(sandbox_omitted);
      resources["mounts_omitted"] = static_cast<long long>(cap_omitted);
      resources["mounts"] = std::move(mounts);
      out["resources"] = std::move(resources);

      Json budget = Json::object();
      budget["workers"] = context.budget.workers;
      budget["cpu_online"] = context.budget.cpu_online;
      budget["buffered_response_bytes"] = static_cast<long long>(
          context.budget.buffered_response_bytes);
      budget["file_read_bytes"] = static_cast<long long>(
          context.budget.file_read_bytes);
      budget["tool_output_bytes"] = static_cast<long long>(
          context.budget.tool_output_bytes);
      budget["file_size_limit"] = static_cast<long long>(
          context.budget.file_size_limit);
      out["budget"] = std::move(budget);

      Json roots = Json::array();
      for (const std::string& root : context.write_roots) {
        roots.push_back(root);
      }
      out["write_roots"] = std::move(roots);
      roots = Json::array();
      for (const std::string& root : context.read_roots) {
        roots.push_back(root);
      }
      out["read_roots"] = std::move(roots);
      out["skills_dir"] = context.skills_dir;
      return out;
    };
    registry.add(std::move(tool));
  }

  {
    Tool tool;
    tool.name = "fs_tree";
    tool.description =
        "Walk a directory inside the read roots, with bounded depth and entry "
        "count. Symlinks are reported, never followed; secret names are marked "
        "denied. Use this to map local architecture and layout.";
    Json properties = Json::object();
    properties["path"] = Json::object();
    properties["path"]["type"] = "string";
    properties["path"]["description"] = "Directory to walk";
    properties["depth"] = Json::object();
    properties["depth"]["type"] = "number";
    properties["depth"]["description"] = "Recursion depth, 1-8 (default 3)";
    tool.parameters = Json::object();
    tool.parameters["type"] = "object";
    tool.parameters["properties"] = properties;
    tool.parameters["required"] = Json::array();
    tool.parameters["required"].push_back("path");
    tool.handler = [context](const Json& arguments) -> Json {
      const std::string requested = arguments["path"].as_string();
      if (requested.empty()) {
        return tool_error("fs_tree requires a path");
      }
      if (!context.read_guard.allows(requested)) {
        return tool_error("path is outside the read roots");
      }
      const std::string root = canonical_path(requested);
      if (root.empty()) {
        return tool_error("cannot resolve " + requested);
      }
      if (looks_secret(root)) {
        return tool_error("path is denied (configuration, credentials or state)");
      }
      int depth = static_cast<int>(arguments["depth"].as_number(3));
      if (depth < 1) depth = 1;
      if (depth > 8) depth = 8;

      std::size_t remaining = kMaxTreeEntries;
      bool truncated = false;
      std::vector<Json> entries;
      walk_tree(root, 1, depth, &remaining, &entries, &truncated);
      std::sort(entries.begin(), entries.end(),
                [](const Json& left, const Json& right) {
                  return left["path"].as_string() < right["path"].as_string();
                });
      Json out = Json::object();
      out["ok"] = true;
      out["root"] = root;
      out["depth"] = depth;
      Json list = Json::array();
      for (Json& entry : entries) {
        list.push_back(std::move(entry));
      }
      out["entries"] = std::move(list);
      out["truncated"] = truncated;
      return out;
    };
    registry.add(std::move(tool));
  }

  {
    Tool tool;
    tool.name = "file_info";
    tool.description =
        "Identify a file inside the read roots: size, mode, sha256, magic, and "
        "ELF/Mach-O/PE headers when present. Read-only; hashes files up to 64 MiB.";
    Json properties = Json::object();
    properties["path"] = Json::object();
    properties["path"]["type"] = "string";
    tool.parameters = Json::object();
    tool.parameters["type"] = "object";
    tool.parameters["properties"] = properties;
    tool.parameters["required"] = Json::array();
    tool.parameters["required"].push_back("path");
    tool.handler = [context](const Json& arguments) -> Json {
      const std::string requested = arguments["path"].as_string();
      std::string path;
      Json error;
      if (!allowed_path(context.read_guard, requested, "file_info", &path,
                        &error)) {
        return error;
      }
      struct stat info {};
      if (stat(path.c_str(), &info) != 0) {
        return tool_error("cannot stat: " + path);
      }
      Json out = Json::object();
      out["ok"] = true;
      out["path"] = path;
      out["size"] = static_cast<long long>(info.st_size);
      char mode[16];
      std::snprintf(mode, sizeof mode, "%04o",
                    static_cast<unsigned>(info.st_mode & 07777));
      out["mode"] = mode;
      out["uid"] = static_cast<long long>(info.st_uid);
      out["gid"] = static_cast<long long>(info.st_gid);
      out["mtime"] = static_cast<long long>(info.st_mtime);
      out["executable"] = (info.st_mode & 0111) != 0;
      out["type"] = S_ISREG(info.st_mode) ? "file"
                     : S_ISDIR(info.st_mode) ? "dir"
                     : S_ISLNK(info.st_mode) ? "link"
                                             : "other";
      if (S_ISREG(info.st_mode) &&
          static_cast<unsigned long long>(info.st_size) <= kMaxHashBytes) {
        const std::string digest = sha256_file_hex(path);
        if (!digest.empty()) {
          out["sha256"] = digest;
        }
      }
      std::string header;
      if (!read_prefix(path, 64, &header)) {
        return tool_error("cannot read: " + path);
      }
      out["magic"] = hex_bytes(header, 16);
      const std::string format = format_name(header);
      out["format"] = format;
      if (format == "elf") {
        add_elf_info(header, &out);
      } else if (format == "mach-o") {
        add_macho_info(header, &out);
      } else if (format == "pe") {
        add_pe_info(header, &out);
      }
      return out;
    };
    registry.add(std::move(tool));
  }

  {
    Tool tool;
    tool.name = "fs_strings";
    tool.description =
        "Extract bounded printable ASCII strings from a file inside the read "
        "roots. Useful for binaries and unknown formats; it does not execute or "
        "decompile the file.";
    Json properties = Json::object();
    properties["path"] = Json::object();
    properties["path"]["type"] = "string";
    properties["min_length"] = Json::object();
    properties["min_length"]["type"] = "number";
    properties["min_length"]["description"] = "Minimum string length, 4-64";
    tool.parameters = Json::object();
    tool.parameters["type"] = "object";
    tool.parameters["properties"] = properties;
    tool.parameters["required"] = Json::array();
    tool.parameters["required"].push_back("path");
    tool.handler = [context](const Json& arguments) -> Json {
      const std::string requested = arguments["path"].as_string();
      std::string path;
      Json error;
      if (!allowed_path(context.read_guard, requested, "fs_strings", &path,
                        &error)) {
        return error;
      }
      int min_length = static_cast<int>(arguments["min_length"].as_number(4));
      if (min_length < 4) min_length = 4;
      if (min_length > 64) min_length = 64;
      std::string data;
      bool truncated = false;
      if (!read_capped(path, kMaxStringsInput, &data, &truncated)) {
        return tool_error("cannot read: " + path);
      }
      Json strings = Json::array();
      std::string current;
      for (std::size_t i = 0; i <= data.size(); ++i) {
        const unsigned char byte =
            i < data.size() ? static_cast<unsigned char>(data[i]) : 0;
        if (byte >= 0x20 && byte <= 0x7e) {
          if (current.size() < 160) {
            current.push_back(static_cast<char>(byte));
          }
        } else {
          if (current.size() >= static_cast<std::size_t>(min_length)) {
            strings.push_back(current);
            if (strings.size() >= kMaxStrings) {
              truncated = true;
              break;
            }
          }
          current.clear();
        }
      }
      Json out = Json::object();
      out["ok"] = true;
      out["path"] = path;
      out["bytes_read"] = static_cast<long long>(data.size());
      out["input_truncated"] = truncated;
      out["strings"] = std::move(strings);
      return out;
    };
    registry.add(std::move(tool));
  }
}

}  // namespace ps5h
