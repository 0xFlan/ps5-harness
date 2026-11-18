#pragma once

#include <cstddef>
#include <string>
#include <vector>

#include "ps5h/resources.h"

namespace ps5h {

// The runtime's share of the measured machine. Worker count tracks online CPUs
// but never asks for more concurrent descriptors than the loader allows;
// buffered response memory is a fraction of what the process can actually see.
struct RuntimeBudget {
  int workers = 1;
  int cpu_online = 1;
  std::size_t buffered_response_bytes = 4u * 1024u * 1024u;
  std::size_t file_read_bytes = 40000;
  std::size_t tool_output_bytes = 20000;  // model context, not console memory
  unsigned long long file_size_limit = 0;  // RLIMIT_FSIZE, 0 = unlimited
};

RuntimeBudget budget_from_profile(const ResourceProfile& profile);

struct FdLimit {
  unsigned long long soft = 0;  // 0 means unlimited
  unsigned long long hard = 0;
  bool raised = false;
  int error = 0;
};

// Raises RLIMIT_NOFILE soft to the hard limit so the harness can use the
// descriptors the loader already permits. Failure is reported, never fatal.
FdLimit raise_fd_limit();

struct StorageBudget {
  unsigned long long capacity = 0;  // bytes writable after the reserve
  unsigned long long free_bytes = 0;
  unsigned long long reserve_bytes = 0;
  bool ok = false;
  int error = 0;
};

// Free space on the filesystem that would receive `path`, minus a reserve that
// keeps logs and state writable. Reports the filesystem's numbers, not guesses.
StorageBudget download_budget(const std::string& path);

std::vector<std::string> describe_budget(const RuntimeBudget& budget);

}  // namespace ps5h
