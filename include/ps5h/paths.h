#pragma once

#include <string>
#include <vector>

namespace ps5h {

// Resolves a path with symlinks and "." / ".." removed. Returns an empty string
// when the path is relative, contains a traversal component that cannot be
// resolved, or has no existing ancestor.
std::string canonical_path(const std::string& path);

// A set of allowed roots. Every filesystem operation the agent can perform goes
// through allows(). A prefix comparison is not enough: /data/homebrewX is not
// inside /data/homebrew, and a symlink inside a root can point anywhere.
class PathGuard {
 public:
  static PathGuard from_roots(const std::vector<std::string>& roots,
                              std::string* error);

  bool allows(const std::string& target) const;
  bool empty() const { return roots_.empty(); }
  const std::vector<std::string>& roots() const { return roots_; }

 private:
  std::vector<std::string> roots_;
};

// Names that must never be read or listed, checked against each path component
// after canonicalisation (A2).
bool looks_secret(const std::string& canonical_path);

}  // namespace ps5h
