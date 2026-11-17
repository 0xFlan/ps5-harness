#pragma once

#include <cstddef>
#include <cstdint>
#include <string>

namespace ps5h {

// Portable SHA-256: the console build must not depend on a system TLS library's
// hash API, and download verification needs a hash.
class Sha256 {
 public:
  Sha256();
  void update(const void* data, std::size_t size);
  std::string hex_digest();

 private:
  void transform(const std::uint8_t* block);

  std::uint32_t state_[8];
  std::uint64_t bit_length_;
  std::uint8_t buffer_[64];
  std::size_t buffer_length_;
};

std::string sha256_hex(const std::string& data);
std::string sha256_file_hex(const std::string& path);

}  // namespace ps5h
