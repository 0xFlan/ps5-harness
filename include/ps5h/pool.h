#pragma once

#include <cstddef>
#include <string>
#include <vector>

#include "ps5h/core.h"

namespace ps5h {

struct ToolJob {
  std::size_t index = 0;
  ToolHandler handler;
  Json arguments;
  int timeout_ms = 30000;
};

struct ToolOutcome {
  std::size_t index = 0;
  Json result;
  std::string error;
};

// Runs the tool calls from one model turn with at most `workers` handler
// threads. Each job keeps its own deadline (run_with_deadline), so a hung tool
// is abandoned exactly as it is for `--call`. Workers are joined before the
// batch returns; no thread outlives the turn.
class ToolExecutor {
 public:
  explicit ToolExecutor(int workers) : workers_(workers > 0 ? workers : 1) {}

  int workers() const { return workers_; }

  std::vector<ToolOutcome> run(const std::vector<ToolJob>& jobs) const;

 private:
  int workers_;
};

}  // namespace ps5h
