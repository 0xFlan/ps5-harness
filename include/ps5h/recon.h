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

// Adds env_info, fs_tree, file_info and fs_strings. All filesystem access is
// read-only and goes through ReconContext::read_guard; the tools never write.
void register_recon_tools(ToolRegistry& registry, const ReconContext& context);

}  // namespace ps5h
