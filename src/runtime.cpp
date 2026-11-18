#include "ps5h/runtime.h"

#include <sys/resource.h>
#include <sys/stat.h>
#include <sys/statvfs.h>
#include <unistd.h>

#include <algorithm>
#include <cerrno>
#include <climits>

namespace ps5h {
namespace {

constexpr unsigned long long kMiB = 1024ULL * 1024ULL;
// Each in-flight tool may hold a socket, a downloaded file and a log handle.
// 24 descriptors stay with the process (stdio, config, plugin handles).
constexpr unsigned long long kFdsPerWorker = 8;
constexpr unsigned long long kFdReserve = 24;

unsigned long long from_rlimit(rlim_t value) {
  return value == RLIM_INFINITY ? 0ULL : static_cast<unsigned long long>(value);
}

std::string limit_text(unsigned long long value) {
  return value == 0 ? "unlimited" : std::to_string(value);
}

std::string directory_of(const std::string& path) {
  struct stat info {};
  if (::stat(path.c_str(), &info) == 0 && S_ISDIR(info.st_mode)) {
    return path;
  }
  const std::size_t slash = path.find_last_of('/');
  if (slash == std::string::npos) {
    return ".";
  }
  if (slash == 0) {
    return "/";
  }
  return path.substr(0, slash);
}

}  // namespace

RuntimeBudget budget_from_profile(const ResourceProfile& profile) {
  RuntimeBudget budget;
  budget.cpu_online = profile.cpu_online > 0 ? profile.cpu_online : 1;
  budget.workers = budget.cpu_online;

  // The fd limit is a real concurrency constraint: every worker can own a
  // socket and a file. Unlimited (0) leaves the CPU count in charge.
  if (profile.limits.nofile_soft > 0) {
    unsigned long long fd_workers = 1;
    if (profile.limits.nofile_soft > kFdReserve) {
      fd_workers = (profile.limits.nofile_soft - kFdReserve) / kFdsPerWorker;
    }
    if (fd_workers < 1) {
      fd_workers = 1;
    }
    budget.workers = std::min<int>(budget.workers,
                                   static_cast<int>(std::min<unsigned long long>(
                                       fd_workers, static_cast<unsigned long long>(INT_MAX))));
  }
  if (budget.workers < 1) {
    budget.workers = 1;
  }

  unsigned long long memory = profile.memory_total_bytes;
  if (profile.memory_available_known && profile.memory_available_bytes > 0) {
    memory = profile.memory_available_bytes;
  }
  if (memory == 0) {
    // No usable memory measurement: 64 MiB keeps the fallback bounded without
    // pretending to know the platform.
    memory = 64ULL * kMiB;
  }
  const unsigned long long buffered = std::max<unsigned long long>(kMiB, memory / 16);
  budget.buffered_response_bytes = static_cast<std::size_t>(
      std::min<unsigned long long>(buffered, static_cast<unsigned long long>(SIZE_MAX)));

  // The provider context, not console memory, bounds how much text is worth
  // reading for a single tool result.
  budget.file_read_bytes = static_cast<std::size_t>(
      std::min<unsigned long long>(std::max<unsigned long long>(kMiB, memory / 16),
                                   4ULL * kMiB));
  budget.file_size_limit = profile.limits.file_size_soft;
  return budget;
}

FdLimit raise_fd_limit() {
  FdLimit result;
  struct rlimit current {};
  if (::getrlimit(RLIMIT_NOFILE, &current) != 0) {
    result.error = errno;
    return result;
  }
  result.soft = from_rlimit(current.rlim_cur);
  result.hard = from_rlimit(current.rlim_max);

  if (current.rlim_cur != current.rlim_max) {
    struct rlimit next = current;
    next.rlim_cur = current.rlim_max;
    if (::setrlimit(RLIMIT_NOFILE, &next) == 0) {
      result.soft = result.hard;
      result.raised = true;
    } else {
      result.error = errno;
    }
  }
  return result;
}

StorageBudget download_budget(const std::string& path) {
  StorageBudget budget;
  const std::string directory = directory_of(path);
  struct statvfs vfs {};
  if (::statvfs(directory.c_str(), &vfs) != 0) {
    budget.error = errno;
    return budget;
  }
  budget.free_bytes =
      static_cast<unsigned long long>(vfs.f_bavail) * static_cast<unsigned long long>(vfs.f_frsize);
  const unsigned long long total =
      static_cast<unsigned long long>(vfs.f_blocks) * static_cast<unsigned long long>(vfs.f_frsize);
  // Keep 1% of the filesystem (up to 64 MiB) for logs, state and the next tool.
  budget.reserve_bytes = std::min<unsigned long long>(total / 100, 64ULL * kMiB);
  budget.capacity =
      budget.free_bytes > budget.reserve_bytes ? budget.free_bytes - budget.reserve_bytes : 0;
  budget.ok = true;
  return budget;
}

std::vector<std::string> describe_budget(const RuntimeBudget& budget) {
  std::vector<std::string> lines;
  lines.push_back("budget workers=" + std::to_string(budget.workers) +
                  " cpu=" + std::to_string(budget.cpu_online) +
                  " buffered=" + format_bytes(budget.buffered_response_bytes) +
                  " file-read=" + format_bytes(budget.file_read_bytes) +
                  " file-size=" + limit_text(budget.file_size_limit));
  return lines;
}

}  // namespace ps5h
