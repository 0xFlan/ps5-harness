#include "ps5h/secrets.h"

#include <algorithm>
#include <cstdlib>

namespace ps5h {

void SecretSet::add(const std::string& value) {
  if (value.size() < 8) {
    return;
  }
  if (std::find(values_.begin(), values_.end(), value) != values_.end()) {
    return;
  }
  values_.push_back(value);
  // Longest first so a secret containing another secret is replaced whole.
  std::sort(values_.begin(), values_.end(),
            [](const std::string& a, const std::string& b) { return a.size() > b.size(); });
}

std::string SecretSet::redact(const std::string& text) const {
  if (values_.empty() || text.empty()) {
    return text;
  }
  std::string out = text;
  for (const auto& value : values_) {
    std::size_t position = 0;
    while ((position = out.find(value, position)) != std::string::npos) {
      out.replace(position, value.size(), "[redacted]");
      position += 10;
    }
  }
  return out;
}

SecretSet SecretSet::from_env(const std::vector<std::string>& names) {
  SecretSet set;
  for (const auto& name : names) {
    if (name.empty()) {
      continue;
    }
    const char* value = std::getenv(name.c_str());
    if (value != nullptr) {
      set.add(value);
    }
  }
  return set;
}

}  // namespace ps5h
