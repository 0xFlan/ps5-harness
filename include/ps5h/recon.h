#pragma once

#include <string>
#include <vector>

#include "ps5h/core.h"
#include "ps5h/paths.h"
#include "ps5h/resources.h"
#include "ps5h/runtime.h"

namespace ps5h {

struct ReconContext {
  PathGuard read_guard;
  ResourceProfile resources;
  RuntimeBudget budget;
  std::string build_id;
  std::string title_id;
  std::vector<std::string> write_roots;
  std::vector<std::string> read_roots;
  std::string skills_dir;
};

// Bounded, allocation-only scanners used by the recon tools. They are public
// so the sanitizer/fuzz harness can exercise the exact production code without
// creating a temporary filesystem tree for every input.
struct StringScanResult {
  std::vector<std::string> strings;
  bool hit_limit = false;
};

namespace recon_detail {

// Inspects at most the bytes supplied. Truncated or hostile headers yield only
// the fields that fit; this function never indexes past the string.
Json describe_header(const std::string& header);

// Extracts printable ASCII runs, clamping min_length to 4..64 and the result
// count to 200. Individual strings are capped at 160 bytes.
StringScanResult scan_strings(const std::string& data, int min_length,
                              std::size_t max_strings);

}  // namespace recon_detail

// Adds env_info, fs_tree, file_info and fs_strings. All filesystem access is
// read-only and goes through ReconContext::read_guard; the tools never write.
void register_recon_tools(ToolRegistry& registry, const ReconContext& context);

}  // namespace ps5h
