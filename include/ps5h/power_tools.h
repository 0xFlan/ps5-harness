#pragma once

#include <cstddef>
#include <string>

#include "ps5h/core.h"
#include "ps5h/paths.h"

namespace ps5h {

struct PowerToolContext {
  PathGuard read_guard;
  PathGuard write_guard;
  std::size_t max_tool_output = 20000;
  LogFn log;
};

// Adds fs_mkdir, fs_move, fs_delete, fs_search, hexdump and sysctl. Every path
// still goes through the configured guards; secret-looking names stay denied.
void register_power_tools(ToolRegistry& registry, const PowerToolContext& context);

}  // namespace ps5h
