// Fuzz driver for toolchains without libFuzzer (Apple clang omits the runtime).
// It calls the same LLVMFuzzerTestOneInput entry points the libFuzzer build uses,
// over seeds plus deterministic mutations, compiled with ASan+UBSan.

#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <vector>

extern "C" int LLVMFuzzerTestOneInput(const std::uint8_t* data, std::size_t size);

namespace {

std::uint64_t next_random(std::uint64_t* state) {
  std::uint64_t x = *state;
  x ^= x << 13;
  x ^= x >> 7;
  x ^= x << 17;
  *state = x;
  return x;
}

void run(const char* text) {
  LLVMFuzzerTestOneInput(reinterpret_cast<const std::uint8_t*>(text), std::strlen(text));
}

}  // namespace

int main(int argc, char** argv) {
  std::size_t iterations = 5000;
  if (argc > 1) {
    iterations = static_cast<std::size_t>(std::strtoul(argv[1], nullptr, 10));
  }

  static const char* kSeeds[] = {
      "{\"a\":1,\"b\":[true,null,\"x\"],\"c\":{\"d\":2.5}}",
      "[[[[[[[[[[[[[[[[[[[[1]]]]]]]]]]]]]]]]]]]]",
      "{\"id\":\"x\",\"baseUrl\":\"https://1.1.1.1\",\"operations\":"
      "{\"o\":{\"path\":\"/{{p}}\",\"pick\":\"a.b\"}}}",
      "{\"\\u0000\":\"\\ud83d\\ude00\",\"dup\":1,\"dup\":2}",
      "{\"a\":\"{{placeholder}}\",\"b\":\"}}{{ \"}",
      "",
      "{",
      "null",
  };
  for (const char* seed : kSeeds) {
    run(seed);
  }

  std::uint64_t state = 0x9e3779b97f4a7c15ull;
  std::vector<std::uint8_t> buffer;
  static const char kAlphabet[] = "{}[]\",:0123456789.eE+-_/\\u{{}}abc \t";

  for (std::size_t i = 0; i < iterations; ++i) {
    const std::size_t size = next_random(&state) % 2048;
    buffer.resize(size);
    const bool structured = (i & 1) == 0;
    for (auto& byte : buffer) {
      const std::uint64_t value = next_random(&state);
      byte = structured ? static_cast<std::uint8_t>(kAlphabet[value % (sizeof(kAlphabet) - 1)])
                        : static_cast<std::uint8_t>(value & 0xFF);
    }
    LLVMFuzzerTestOneInput(buffer.data(), buffer.size());
  }

  std::printf("driver completed %zu iterations\n", iterations);
  return 0;
}
