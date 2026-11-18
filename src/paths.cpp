#include "ps5h/paths.h"

#include <climits>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

#include <unistd.h>

namespace ps5h {

std::string canonical_path(const std::string& path) {
  if (path.empty() || path.front() != '/') {
    return std::string();
  }

  char resolved[PATH_MAX];
  if (::realpath(path.c_str(), resolved) != nullptr) {
    return std::string(resolved);
  }

  // Target does not exist yet: canonicalise the nearest existing ancestor and
  // re-append the remaining components, rejecting traversal.
  std::string current = path;
  std::vector<std::string> tail;
  while (true) {
    if (current.empty() || current == "/") {
      break;
    }
    std::size_t slash = current.find_last_of('/');
    if (slash == std::string::npos) {
      return std::string();
    }
    std::string base = current.substr(slash + 1);
    if (base.empty() || base == "." || base == "..") {
      return std::string();
    }
    tail.push_back(base);
    current = slash == 0 ? "/" : current.substr(0, slash);

    if (::realpath(current.c_str(), resolved) != nullptr) {
      std::string out = resolved;
      for (auto it = tail.rbegin(); it != tail.rend(); ++it) {
        if (out.empty() || out.back() != '/') {
          out.push_back('/');
        }
        out += *it;
      }
      return out;
    }
  }
  return std::string();
}

PathGuard PathGuard::from_roots(const std::vector<std::string>& roots,
                                std::string* error) {
  PathGuard guard;
  for (const auto& root : roots) {
    if (root.empty()) {
      continue;
    }
    std::string canonical = canonical_path(root);
    if (canonical.empty()) {
      if (error != nullptr) {
        *error = "allowed root does not exist or is not absolute: " + root;
      }
      return PathGuard{};
    }
    while (canonical.size() > 1 && canonical.back() == '/') {
      canonical.pop_back();
    }
    guard.roots_.push_back(canonical);
  }
  if (guard.roots_.empty() && error != nullptr) {
    *error = "no usable allowed roots";
  }
  return guard;
}

bool PathGuard::allows(const std::string& target) const {
  if (roots_.empty()) {
    return false;
  }
  std::string canonical = canonical_path(target);
  if (canonical.empty()) {
    return false;
  }
  for (const auto& root : roots_) {
    if (canonical == root) {
      return true;
    }
    if (canonical.size() > root.size() &&
        canonical.compare(0, root.size(), root) == 0 &&
        canonical[root.size()] == '/') {
      return true;
    }
  }
  return false;
}

bool looks_secret(const std::string& canonical_path) {
  static const char* kDenied[] = {"config.json",  "state.db", "logs",
                                  "secrets",      "token",    "credential",
                                  "credentials",  ".ssh",     "auth.json",
                                  "id_rsa",       "id_ed25519",
                                  "ps5-harness"};
  std::string lower;
  lower.reserve(canonical_path.size());
  for (unsigned char c : canonical_path) {
    lower.push_back(static_cast<char>(c >= 'A' && c <= 'Z' ? c + 32 : c));
  }
  if (lower.find(".key") != std::string::npos || lower.find(".pem") != std::string::npos) {
    return true;
  }
  for (const char* needle : kDenied) {
    if (lower.find(needle) != std::string::npos) {
      return true;
    }
  }
  return false;
}

}  // namespace ps5h
