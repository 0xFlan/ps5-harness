#include "ps5h/resources.h"

#include <sys/mount.h>
#include <sys/resource.h>
#include <sys/statvfs.h>
#include <unistd.h>

#include <cerrno>
#include <cstdlib>

namespace ps5h {
namespace {

unsigned long long from_rlimit(rlim_t value) {
  return value == RLIM_INFINITY ? 0ULL : static_cast<unsigned long long>(value);
}

unsigned long long blocks_to_bytes(unsigned long long blocks,
                                   unsigned long long block_size) {
  if (block_size == 0) {
    return 0;
  }
  return blocks * block_size;
}

MountSpace from_statfs(const struct statfs& entry) {
  MountSpace space;
  space.path = entry.f_mntonname;
  space.type = entry.f_fstypename;
  space.free_bytes =
      blocks_to_bytes(static_cast<unsigned long long>(entry.f_bavail),
                      static_cast<unsigned long long>(entry.f_bsize));
  space.total_bytes =
      blocks_to_bytes(static_cast<unsigned long long>(entry.f_blocks),
                      static_cast<unsigned long long>(entry.f_bsize));
  space.ok = true;
  return space;
}

MountSpace from_statvfs(const std::string& path) {
  MountSpace space;
  space.path = path;
  struct statvfs vfs {};
  if (::statvfs(path.c_str(), &vfs) != 0) {
    space.error = errno;
    return space;
  }
  space.free_bytes =
      blocks_to_bytes(static_cast<unsigned long long>(vfs.f_bavail),
                      static_cast<unsigned long long>(vfs.f_frsize));
  space.total_bytes =
      blocks_to_bytes(static_cast<unsigned long long>(vfs.f_blocks),
                      static_cast<unsigned long long>(vfs.f_frsize));
  space.ok = true;
  return space;
}

std::string limit_text(unsigned long long value) {
  return value == 0 ? "unlimited" : std::to_string(value);
}

}  // namespace

std::string format_bytes(unsigned long long bytes) {
  return std::to_string(bytes / (1024ULL * 1024ULL)) + " MiB";
}

ResourceProfile probe_resources(const std::vector<std::string>& mount_paths) {
  ResourceProfile profile;

  const long online = ::sysconf(_SC_NPROCESSORS_ONLN);
  if (online > 0) {
    profile.cpu_online = static_cast<int>(online);
  }
#ifdef _SC_NPROCESSORS_CONF
  const long configured = ::sysconf(_SC_NPROCESSORS_CONF);
  if (configured > 0) {
    profile.cpu_configured = static_cast<int>(configured);
  } else
#endif
  {
    profile.cpu_configured = profile.cpu_online;
  }

  const long page_size = ::sysconf(_SC_PAGESIZE);
  if (page_size > 0) {
    profile.page_bytes = static_cast<unsigned long long>(page_size);
  }
  const long total_pages = ::sysconf(_SC_PHYS_PAGES);
  if (total_pages > 0 && page_size > 0) {
    profile.memory_total_bytes = static_cast<unsigned long long>(total_pages) *
                                 static_cast<unsigned long long>(page_size);
  }
#ifdef _SC_AVPHYS_PAGES
  const long available_pages = ::sysconf(_SC_AVPHYS_PAGES);
  if (available_pages > 0 && page_size > 0) {
    profile.memory_available_bytes =
        static_cast<unsigned long long>(available_pages) *
        static_cast<unsigned long long>(page_size);
    profile.memory_available_known = true;
  }
#endif

  struct rlimit nofile {};
  if (::getrlimit(RLIMIT_NOFILE, &nofile) == 0) {
    profile.limits.nofile_soft = from_rlimit(nofile.rlim_cur);
    profile.limits.nofile_hard = from_rlimit(nofile.rlim_max);
  }
#ifdef RLIMIT_AS
  struct rlimit address_space {};
  if (::getrlimit(RLIMIT_AS, &address_space) == 0) {
    profile.limits.address_space_soft = from_rlimit(address_space.rlim_cur);
    profile.limits.address_space_hard = from_rlimit(address_space.rlim_max);
  }
#endif
#ifdef RLIMIT_DATA
  struct rlimit data {};
  if (::getrlimit(RLIMIT_DATA, &data) == 0) {
    profile.limits.data_soft = from_rlimit(data.rlim_cur);
    profile.limits.data_hard = from_rlimit(data.rlim_max);
  }
#endif
#ifdef RLIMIT_FSIZE
  struct rlimit file_size {};
  if (::getrlimit(RLIMIT_FSIZE, &file_size) == 0) {
    profile.limits.file_size_soft = from_rlimit(file_size.rlim_cur);
    profile.limits.file_size_hard = from_rlimit(file_size.rlim_max);
  }
#endif

  struct statfs* entries = nullptr;
  const int count = ::getmntinfo(&entries, MNT_NOWAIT);
  if (count > 0 && entries != nullptr) {
    profile.mounts.reserve(static_cast<std::size_t>(count));
    for (int i = 0; i < count; ++i) {
      profile.mounts.push_back(from_statfs(entries[i]));
    }
  }
  // FreeBSD's getmntinfo hands ownership to the caller (the SDK's mntinfo
  // sample frees it). macOS keeps ownership of a static buffer and documents
  // that it must not be freed; doing so traps.
#if !defined(__APPLE__)
  std::free(entries);
#endif

  profile.candidates.reserve(mount_paths.size());
  for (const std::string& path : mount_paths) {
    profile.candidates.push_back(from_statvfs(path));
  }
  return profile;
}

std::vector<std::string> describe_resources(const ResourceProfile& profile) {
  std::vector<std::string> lines;
  lines.push_back("cpu online=" + std::to_string(profile.cpu_online) +
                  " configured=" + std::to_string(profile.cpu_configured));
  const std::string available = profile.memory_available_known
                                    ? format_bytes(profile.memory_available_bytes)
                                    : "unknown";
  lines.push_back("mem total=" + format_bytes(profile.memory_total_bytes) +
                  " available=" + available +
                  " page=" + std::to_string(profile.page_bytes) + " B");
  lines.push_back("limit nofile=" + limit_text(profile.limits.nofile_soft) + "/" +
                  limit_text(profile.limits.nofile_hard) +
                  " as=" + limit_text(profile.limits.address_space_soft) + "/" +
                  limit_text(profile.limits.address_space_hard) +
                  " data=" + limit_text(profile.limits.data_soft) + "/" +
                  limit_text(profile.limits.data_hard));

  for (const MountSpace& mount : profile.mounts) {
    lines.push_back("mount " + mount.path +
                    " type=" + (mount.type.empty() ? "unknown" : mount.type) +
                    " free=" + format_bytes(mount.free_bytes) +
                    " total=" + format_bytes(mount.total_bytes));
  }
  for (const MountSpace& candidate : profile.candidates) {
    if (!candidate.ok) {
      lines.push_back("candidate " + candidate.path +
                      " unavailable errno=" + std::to_string(candidate.error));
      continue;
    }
    lines.push_back("candidate " + candidate.path + " free=" +
                    format_bytes(candidate.free_bytes) +
                    " total=" + format_bytes(candidate.total_bytes));
  }
  return lines;
}

}  // namespace ps5h
