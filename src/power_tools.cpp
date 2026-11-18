#include "ps5h/power_tools.h"

#include <dirent.h>
#include <sys/stat.h>
#include <unistd.h>

#include <algorithm>
#include <cerrno>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <functional>
#include <limits>
#include <string>
#include <vector>

#if defined(__APPLE__) || defined(__FreeBSD__)
#include <sys/sysctl.h>
#define PS5H_HAVE_SYSCTL 1
#else
#define PS5H_HAVE_SYSCTL 0
#endif

namespace ps5h {
namespace {

constexpr std::size_t kSearchFileBytes = 1024u * 1024u;
constexpr std::size_t kSearchTotalBytes = 8u * 1024u * 1024u;
constexpr std::size_t kSearchFiles = 2000u;

Json refuse(const std::string& reason) {
  return Json(std::string("refused: ") + reason);
}

Json failure(const std::string& operation, int error) {
  return Json(operation + " failed: " + std::strerror(error) +
              " (errno=" + std::to_string(error) + ")");
}

long long bounded_number(double value, long long fallback, long long minimum,
                         long long maximum) {
  if (!std::isfinite(value)) return fallback;
  if (value <= static_cast<double>(minimum)) return minimum;
  if (value >= static_cast<double>(maximum)) return maximum;
  return static_cast<long long>(value);
}

std::string path_join(const std::string& parent, const std::string& child) {
  if (parent.empty()) return child;
  if (parent.back() == '/') return parent + child;
  return parent + "/" + child;
}

bool read_text_file(const std::string& path, std::string* out) {
  FILE* file = std::fopen(path.c_str(), "rb");
  if (file == nullptr) return false;
  char buffer[8192];
  std::size_t got = 0;
  bool binary = false;
  while ((got = std::fread(buffer, 1, sizeof buffer, file)) > 0) {
    for (std::size_t i = 0; i < got; ++i) {
      if (buffer[i] == '\0') binary = true;
    }
    if (out->size() + got > kSearchFileBytes) {
      std::fclose(file);
      return false;
    }
    out->append(buffer, got);
  }
  const bool failed = std::ferror(file) != 0;
  std::fclose(file);
  return !failed && !binary;
}

std::string lower_ascii(std::string value) {
  for (char& c : value) {
    if (c >= 'A' && c <= 'Z') c = static_cast<char>(c - 'A' + 'a');
  }
  return value;
}

[[maybe_unused]] std::string hex_value(const unsigned char* data,
                                       std::size_t size) {
  static constexpr char kHex[] = "0123456789abcdef";
  std::string out;
  out.reserve(size * 2);
  for (std::size_t i = 0; i < size; ++i) {
    out.push_back(kHex[data[i] >> 4]);
    out.push_back(kHex[data[i] & 0x0f]);
  }
  return out;
}

}  // namespace

void register_power_tools(ToolRegistry& registry, const PowerToolContext& context) {
  const PathGuard read_guard = context.read_guard;
  const PathGuard write_guard = context.write_guard;
  const std::size_t max_output = context.max_tool_output;
  const LogFn log = context.log;

  {
    Tool tool;
    tool.name = "fs_mkdir";
    tool.description =
        "Create one directory inside the allowed write roots. The parent must "
        "already exist; no recursive creation.";
    tool.parameters["type"] = "object";
    tool.parameters["properties"]["path"]["type"] = "string";
    tool.parameters["required"] = Json::array();
    tool.parameters["required"].push_back("path");
    tool.timeout_ms = 10000;
    tool.handler = [write_guard](const Json& arguments) -> Json {
      const std::string requested = arguments["path"].as_string();
      if (!write_guard.allows(requested)) {
        return refuse("directory is outside the write roots");
      }
      const std::string path = canonical_path(requested);
      if (path.empty()) return refuse("cannot resolve directory");
      if (looks_secret(path)) return refuse("directory name is denied");
      struct stat info {};
      if (::lstat(path.c_str(), &info) == 0) {
        if (S_ISDIR(info.st_mode)) return Json(path + " already exists");
        return refuse("path exists and is not a directory");
      }
      if (::mkdir(path.c_str(), 0755) != 0) return failure("mkdir", errno);
      return Json(path);
    };
    registry.add(std::move(tool));
  }

  {
    Tool tool;
    tool.name = "fs_move";
    tool.description =
        "Rename or move a file or directory within the write roots. Refuses to "
        "overwrite unless overwrite=true; cross-device moves are not split.";
    tool.parameters["type"] = "object";
    tool.parameters["properties"]["source"]["type"] = "string";
    tool.parameters["properties"]["destination"]["type"] = "string";
    tool.parameters["properties"]["overwrite"]["type"] = "boolean";
    tool.parameters["required"] = Json::array();
    tool.parameters["required"].push_back("source");
    tool.parameters["required"].push_back("destination");
    tool.timeout_ms = 30000;
    tool.handler = [write_guard](const Json& arguments) -> Json {
      const std::string source = canonical_path(arguments["source"].as_string());
      const std::string destination =
          canonical_path(arguments["destination"].as_string());
      if (source.empty() || destination.empty()) return refuse("cannot resolve path");
      if (!write_guard.allows(source) || !write_guard.allows(destination)) {
        return refuse("both paths must be inside the write roots");
      }
      if (looks_secret(source) || looks_secret(destination)) {
        return refuse("path name is denied");
      }
      if (source == destination) return refuse("source and destination are identical");
      struct stat source_info {};
      if (::lstat(source.c_str(), &source_info) != 0) {
        return failure("stat source", errno);
      }
      struct stat destination_info {};
      const bool destination_exists =
          ::lstat(destination.c_str(), &destination_info) == 0;
      if (destination_exists && !arguments["overwrite"].as_bool(false)) {
        return refuse("destination exists; pass overwrite=true to replace it");
      }
      if (::rename(source.c_str(), destination.c_str()) != 0) {
        return failure("rename", errno);
      }
      return Json(source + " -> " + destination);
    };
    registry.add(std::move(tool));
  }

  {
    Tool tool;
    tool.name = "fs_delete";
    tool.description =
        "Delete one file or one empty directory inside the write roots. Never "
        "recursive; symlinks are removed as links, not followed.";
    tool.parameters["type"] = "object";
    tool.parameters["properties"]["path"]["type"] = "string";
    tool.parameters["required"] = Json::array();
    tool.parameters["required"].push_back("path");
    tool.timeout_ms = 10000;
    tool.handler = [write_guard](const Json& arguments) -> Json {
      const std::string path = canonical_path(arguments["path"].as_string());
      if (path.empty()) return refuse("cannot resolve path");
      if (!write_guard.allows(path)) return refuse("path is outside the write roots");
      if (looks_secret(path)) return refuse("path name is denied");
      struct stat info {};
      if (::lstat(path.c_str(), &info) != 0) return failure("stat", errno);
      int result = 0;
      if (S_ISDIR(info.st_mode)) {
        result = ::rmdir(path.c_str());
      } else {
        result = ::unlink(path.c_str());
      }
      if (result != 0) return failure("delete", errno);
      return Json("deleted " + path);
    };
    registry.add(std::move(tool));
  }

  {
    Tool tool;
    tool.name = "fs_search";
    tool.description =
        "Search text files under a directory for a literal string. Bounded "
        "recursion, file count and bytes; returns path:line:column matches.";
    tool.parameters["type"] = "object";
    tool.parameters["properties"]["path"]["type"] = "string";
    tool.parameters["properties"]["query"]["type"] = "string";
    tool.parameters["properties"]["case_sensitive"]["type"] = "boolean";
    tool.parameters["properties"]["max_depth"]["type"] = "integer";
    tool.parameters["properties"]["max_results"]["type"] = "integer";
    tool.parameters["required"] = Json::array();
    tool.parameters["required"].push_back("path");
    tool.parameters["required"].push_back("query");
    tool.timeout_ms = 30000;
    tool.handler = [read_guard, max_output, log](const Json& arguments) -> Json {
      const std::string query = arguments["query"].as_string();
      if (query.empty() || query.size() > 200) {
        return refuse("query must be 1..200 bytes");
      }
      const std::string root = canonical_path(arguments["path"].as_string());
      if (root.empty() || !read_guard.allows(root)) {
        return refuse("search root is outside the read roots");
      }
      struct stat root_info {};
      if (::lstat(root.c_str(), &root_info) != 0 || !S_ISDIR(root_info.st_mode)) {
        return refuse("search root must be an existing directory");
      }
      const bool case_sensitive = arguments["case_sensitive"].as_bool(false);
      const int max_depth = static_cast<int>(bounded_number(
          arguments["max_depth"].as_number(4), 4, 0, 8));
      const int max_results = static_cast<int>(bounded_number(
          arguments["max_results"].as_number(40), 40, 1, 100));
      const std::string needle = case_sensitive ? query : lower_ascii(query);
      std::size_t files = 0;
      std::size_t bytes = 0;
      std::size_t skipped = 0;
      std::string out;
      bool truncated = false;

      std::function<void(const std::string&, int)> walk =
          [&](const std::string& directory, int depth) {
            if (truncated || files >= kSearchFiles || bytes >= kSearchTotalBytes) {
              truncated = true;
              return;
            }
            DIR* dir = ::opendir(directory.c_str());
            if (dir == nullptr) {
              ++skipped;
              return;
            }
            while (dirent* entry = ::readdir(dir)) {
              if (std::strcmp(entry->d_name, ".") == 0 ||
                  std::strcmp(entry->d_name, "..") == 0) {
                continue;
              }
              const std::string path = path_join(directory, entry->d_name);
              if (looks_secret(path)) {
                ++skipped;
                continue;
              }
              struct stat info {};
              if (::lstat(path.c_str(), &info) != 0) continue;
              if (S_ISLNK(info.st_mode)) continue;
              if (S_ISDIR(info.st_mode)) {
                if (depth < max_depth) walk(path, depth + 1);
                continue;
              }
              if (!S_ISREG(info.st_mode)) continue;
              if (static_cast<unsigned long long>(info.st_size) > kSearchFileBytes) {
                ++skipped;
                continue;
              }
              if (files >= kSearchFiles || bytes >= kSearchTotalBytes) {
                truncated = true;
                break;
              }
              ++files;
              std::string text;
              if (!read_text_file(path, &text)) {
                ++skipped;
                continue;
              }
              bytes += text.size();
              std::size_t line = 1;
              std::size_t line_start = 0;
              while (line_start <= text.size()) {
                const std::size_t line_end = text.find('\n', line_start);
                const std::size_t end =
                    line_end == std::string::npos ? text.size() : line_end;
                const std::string line_text = text.substr(line_start, end - line_start);
                const std::string haystack =
                    case_sensitive ? line_text : lower_ascii(line_text);
                const std::size_t column = haystack.find(needle);
                if (column != std::string::npos && out.size() < max_output) {
                  out += path + ":" + std::to_string(line) + ":" +
                         std::to_string(column + 1) + ": " +
                         clip(line_text, 240) + "\n";
                  if (out.size() >= max_output) {
                    truncated = true;
                    break;
                  }
                }
                if (line_end == std::string::npos) break;
                line_start = line_end + 1;
                ++line;
                if (line > 1000000) {
                  truncated = true;
                  break;
                }
              }
              if (truncated) break;
              if (out.size() >= max_output) {
                truncated = true;
                break;
              }
              if (out.find('\n') != std::string::npos) {
                // Count output lines cheaply only for the cap; the content
                // itself is the useful evidence.
                const std::size_t matches =
                    static_cast<std::size_t>(std::count(out.begin(), out.end(), '\n'));
                if (matches >= static_cast<std::size_t>(max_results)) {
                  truncated = true;
                  break;
                }
              }
            }
            ::closedir(dir);
          };
      walk(root, 0);
      log("fs_search scanned " + std::to_string(files) + " file(s), " +
          std::to_string(bytes) + " byte(s)");
      if (out.empty()) {
        out = "no matches";
      }
      if (truncated) {
        out += "\n[search stopped at a bound; narrow the query or path]";
      }
      if (skipped > 0) {
        out += "\n[skipped " + std::to_string(skipped) +
               " binary/denied/oversized path(s)]";
      }
      return Json(clip(out, max_output));
    };
    registry.add(std::move(tool));
  }

  {
    Tool tool;
    tool.name = "hexdump";
    tool.description =
        "Render a bounded hexadecimal and ASCII view of a file inside the read "
        "roots. Useful for headers, tables and opaque structures.";
    tool.parameters["type"] = "object";
    tool.parameters["properties"]["path"]["type"] = "string";
    tool.parameters["properties"]["offset"]["type"] = "integer";
    tool.parameters["properties"]["length"]["type"] = "integer";
    tool.parameters["required"] = Json::array();
    tool.parameters["required"].push_back("path");
    tool.timeout_ms = 10000;
    tool.handler = [read_guard, max_output](const Json& arguments) -> Json {
      const std::string path = canonical_path(arguments["path"].as_string());
      if (path.empty() || !read_guard.allows(path)) {
        return refuse("path is outside the read roots");
      }
      if (looks_secret(path)) return refuse("path name is denied");
      FILE* file = std::fopen(path.c_str(), "rb");
      if (file == nullptr) return failure("open", errno);
      if (std::fseek(file, 0, SEEK_END) != 0) {
        std::fclose(file);
        return failure("seek", errno);
      }
      const long file_size = std::ftell(file);
      if (file_size < 0) {
        std::fclose(file);
        return failure("tell", errno);
      }
      const long long offset = bounded_number(
          arguments["offset"].as_number(0), 0, 0,
          static_cast<long long>(file_size));
      if (offset > file_size) {
        std::fclose(file);
        return refuse("offset is outside the file");
      }
      long long length = bounded_number(
          arguments["length"].as_number(256), 256, 1, 8192);
      length = std::min<long long>(length, file_size - offset);
      if (std::fseek(file, static_cast<long>(offset), SEEK_SET) != 0) {
        std::fclose(file);
        return failure("seek", errno);
      }
      std::vector<unsigned char> data(static_cast<std::size_t>(length));
      const std::size_t got = std::fread(data.data(), 1, data.size(), file);
      const bool failed = std::ferror(file) != 0;
      std::fclose(file);
      if (failed) return failure("read", errno);
      data.resize(got);
      std::string out;
      for (std::size_t base = 0; base < data.size(); base += 16) {
        char address[32] = {};
        std::snprintf(address, sizeof address, "%08llx",
                      static_cast<unsigned long long>(offset) + base);
        out += address;
        out += "  ";
        for (std::size_t i = 0; i < 16; ++i) {
          if (base + i < data.size()) {
            char byte[4] = {};
            std::snprintf(byte, sizeof byte, "%02x ", data[base + i]);
            out += byte;
          } else {
            out += "   ";
          }
          if (i == 7) out += ' ';
        }
        out += " |";
        for (std::size_t i = 0; i < 16 && base + i < data.size(); ++i) {
          const unsigned char c = data[base + i];
          out += (c >= 32 && c < 127) ? static_cast<char>(c) : '.';
        }
        out += "|\n";
        if (out.size() >= max_output) break;
      }
      if (data.empty()) out = "(empty range)";
      return Json(clip(out, max_output));
    };
    registry.add(std::move(tool));
  }

  {
    Tool tool;
    tool.name = "sysctl";
    tool.description =
        "Read a bounded FreeBSD/macOS sysctl value by name (kern.*, hw.*, vm.*, "
        "net.*, machdep.* or dev.*). This is a research probe, not a shell.";
    tool.parameters["type"] = "object";
    tool.parameters["properties"]["name"]["type"] = "string";
    tool.parameters["required"] = Json::array();
    tool.parameters["required"].push_back("name");
    tool.timeout_ms = 10000;
    tool.handler = [max_output](const Json& arguments) -> Json {
      (void)max_output;
      const std::string name = arguments["name"].as_string();
      if (name.empty() || name.size() > 128) return refuse("invalid sysctl name");
      static const char* const prefixes[] = {"kern.", "hw.", "vm.", "net.",
                                             "machdep.", "dev."};
      bool allowed = false;
      for (const char* prefix : prefixes) {
        if (name.rfind(prefix, 0) == 0) {
          allowed = true;
          break;
        }
      }
      if (!allowed) return refuse("sysctl namespace is not allowlisted");
      const std::string lowered = lower_ascii(name);
      for (const char* denied : {"args", "env", "cred", "key", "token",
                                 "password", "secret"}) {
        if (lowered.find(denied) != std::string::npos) {
          return refuse("sysctl name is denied");
        }
      }
#if PS5H_HAVE_SYSCTL
      std::size_t size = 0;
      if (::sysctlbyname(name.c_str(), nullptr, &size, nullptr, 0) != 0) {
        return failure("sysctlbyname size", errno);
      }
      if (size == 0) return Json(std::string());
      const std::size_t cap = 8192;
      if (size > cap) size = cap;
      std::vector<unsigned char> data(size);
      std::size_t actual = size;
      if (::sysctlbyname(name.c_str(), data.data(), &actual, nullptr, 0) != 0) {
        return failure("sysctlbyname", errno);
      }
      data.resize(actual);
      bool printable = actual > 0;
      for (unsigned char c : data) {
        if (c == 0) {
          printable = false;
          break;
        }
        if (c < 9 || (c > 13 && c < 32) || c >= 127) {
          printable = false;
          break;
        }
      }
      if (printable) {
        std::string text(data.begin(), data.end());
        while (!text.empty() && (text.back() == '\n' || text.back() == '\r')) {
          text.pop_back();
        }
        return Json(clip(text, max_output));
      }
      if (actual == 1 || actual == 2 || actual == 4 || actual == 8) {
        unsigned long long value = 0;
        for (std::size_t i = 0; i < actual; ++i) {
          value |= static_cast<unsigned long long>(data[i]) << (8 * i);
        }
        return Json(std::to_string(value));
      }
      return Json(hex_value(data.data(), std::min<std::size_t>(data.size(), 2048)));
#else
      (void)max_output;
      return Json("sysctl is unavailable on this host build; run on the console");
#endif
    };
    registry.add(std::move(tool));
  }
}

}  // namespace ps5h
