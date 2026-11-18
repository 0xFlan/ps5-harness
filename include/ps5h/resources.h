#pragma once

#include <string>
#include <vector>

namespace ps5h {

struct MountSpace {
  std::string path;
  std::string type;
  unsigned long long free_bytes = 0;
  unsigned long long total_bytes = 0;
  bool ok = false;
  int error = 0;
};

// 0 means RLIM_INFINITY / unlimited.
struct ResourceLimits {
  unsigned long long nofile_soft = 0;
  unsigned long long nofile_hard = 0;
  unsigned long long address_space_soft = 0;
  unsigned long long address_space_hard = 0;
  unsigned long long data_soft = 0;
  unsigned long long data_hard = 0;
  unsigned long long file_size_soft = 0;
  unsigned long long file_size_hard = 0;
};

// What the process can actually use. On the console this is the answer to
// "what does an agent living here inherit": online CPUs (not the console's
// marketing core count), the memory the process can see, the fd/address-space
// limits the loader imposed, and every mounted filesystem with its free space.
struct ResourceProfile {
  int cpu_online = 1;
  int cpu_configured = 1;
  unsigned long long page_bytes = 0;
  unsigned long long memory_total_bytes = 0;
  unsigned long long memory_available_bytes = 0;
  // macOS does not expose _SC_AVPHYS_PAGES; report "unknown" rather than 0.
  bool memory_available_known = false;
  ResourceLimits limits;
  std::vector<MountSpace> mounts;      // every getmntinfo() entry
  std::vector<MountSpace> candidates;  // each explicitly probed path
};

// mount_paths may be empty. Enumeration failures are reported in the profile;
// the probe never throws and never aborts the caller.
ResourceProfile probe_resources(const std::vector<std::string>& mount_paths);

// Human-readable lines without trailing newlines: cpu, memory, limits, then
// one line per mount. Stable prefixes (`cpu `, `mem `, `limit `, `mount `,
// `candidate `) so console logs can be grepped and quoted in reports.
std::vector<std::string> describe_resources(const ResourceProfile& profile);

std::string format_bytes(unsigned long long bytes);

}  // namespace ps5h
