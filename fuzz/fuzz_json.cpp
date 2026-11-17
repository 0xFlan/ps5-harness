// Fuzz target: the hand-written parser handles untrusted bytes on every tool
// response and every manifest. A crash here is a remote crash on the console.

#include <cstddef>
#include <cstdint>
#include <string>

#include "ps5h/json.h"

extern "C" int LLVMFuzzerTestOneInput(const std::uint8_t* data, std::size_t size) {
  std::string input(reinterpret_cast<const char*>(data), size);
  try {
    ps5h::Json value = ps5h::Json::parse(input);
    const std::string serialised = value.dump();
    ps5h::Json reparsed = ps5h::Json::parse(serialised);
    (void)reparsed;
    (void)value.pick("response.docs.0.identifier");
    (void)value.dump(2);
  } catch (const ps5h::JsonError&) {
    // Rejected input is the expected outcome for most mutations.
  }
  return 0;
}
