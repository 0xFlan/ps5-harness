#pragma once

#include <string>
#include <vector>

namespace ps5h {

// Redacts known secret values out of anything that could reach the model
// context, the log, or a transcript (A2).
//
// Secrets are never stored in config: config names the environment variable.
// This class holds the values only so it can find them again in output.
class SecretSet {
 public:
  void add(const std::string& value);
  bool empty() const { return values_.empty(); }

  // Replaces every occurrence with "[redacted]". Values shorter than 8 bytes are
  // ignored on insertion so that redaction cannot shred ordinary text.
  std::string redact(const std::string& text) const;

  static SecretSet from_env(const std::vector<std::string>& names);

 private:
  std::vector<std::string> values_;
};

}  // namespace ps5h
