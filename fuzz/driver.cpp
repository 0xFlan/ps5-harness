// Fuzz driver for toolchains without libFuzzer (Apple clang omits the runtime).
// It calls the same LLVMFuzzerTestOneInput entry points the libFuzzer build uses,
// over seeds plus deterministic mutations, compiled with ASan+UBSan.

#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <utility>
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

void run_binary(std::vector<std::uint8_t> bytes) {
  LLVMFuzzerTestOneInput(bytes.data(), bytes.size());
}

void run_adversarial_seeds() {
  // ELF/Mach-O/PE prefix and hostile-field seeds. The PE cases make
  // e_lfanew + 4 wrap, which used to reach string::compare with an impossible
  // position. Truncated prefixes ensure no read runs past the supplied bytes.
  for (std::size_t size = 0; size <= 64; ++size) {
    run_binary(std::vector<std::uint8_t>(size, 0));
  }

  std::vector<std::uint8_t> elf(64, 0);
  elf[0] = 0x7f;
  elf[1] = 'E';
  elf[2] = 'L';
  elf[3] = 'F';
  elf[4] = 2;
  elf[5] = 1;
  elf[16] = 3;
  for (std::size_t size = 0; size <= elf.size(); ++size) {
    run_binary(std::vector<std::uint8_t>(elf.begin(),
                                         elf.begin() + static_cast<long>(size)));
  }

  const std::uint32_t offsets[] = {0xffffffffu, 0xfffffffeu, 0xfffffffdu,
                                   0xfffffffcu, 0x40u, 60u, 0u};
  for (std::uint32_t offset : offsets) {
    std::vector<std::uint8_t> pe(64, 0);
    pe[0] = 'M';
    pe[1] = 'Z';
    for (int i = 0; i < 4; ++i) {
      pe[0x3c + static_cast<std::size_t>(i)] =
          static_cast<std::uint8_t>((offset >> (8 * i)) & 0xff);
    }
    run_binary(std::move(pe));
  }

  static const char* kMissionSeeds[] = {
      "{\"steps\":[{\"profile\":\"a\",\"task\":\"b\"}]}",
      "{\"steps\":[{\"profile\":\"a\",\"task\":\"b\","
      "\"max_steps\":1e999}]}",
      "{\"steps\":[{\"profile\":\"a\",\"task\":\"b\","
      "\"max_tokens\":-1e999}]}",
      "{\"steps\":[{\"swarm\":[{\"profile\":\"a\",\"task\":\"b\","
      "\"max_steps\":1e999}]}]}",
      "{\"steps\":[{\"swarm\":[{\"profile\":\"a\",\"task\":\"b\"}]}]}",
  };
  for (const char* seed : kMissionSeeds) {
    run(seed);
  }
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
  run_adversarial_seeds();

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
