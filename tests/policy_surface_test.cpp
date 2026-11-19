// Compile-time proof that the test-only loopback policy does not exist in a
// production build. Built WITHOUT -DPS5H_TEST_HOOKS; if the field were ever
// compiled in unconditionally, this file would fail to compile.

#include <cassert>
#include <cstdio>
#include <string>
#include <type_traits>

#include "ps5h/policy.h"

namespace {

template <typename T, typename = void>
struct has_test_hook : std::false_type {};

template <typename T>
struct has_test_hook<T, std::void_t<decltype(T::test_allow_loopback_cleartext)>>
    : std::true_type {};

static_assert(!has_test_hook<ps5h::UrlPolicy>::value,
              "test-only loopback hook must not exist in production builds");

bool local_ok(const std::string& url) {
  std::string reason;
  const bool ok = ps5h::local_model_endpoint(url, &reason);
  if (!ok) std::fprintf(stderr, "rejected %s: %s\n", url.c_str(), reason.c_str());
  return ok;
}

void local_model_endpoint_test() {
  assert(local_ok("http://127.0.0.1:8081/v1/chat/completions"));
  assert(local_ok("http://localhost:8081/v1/chat/completions"));
  assert(local_ok("http://[::1]:8081/v1/chat/completions"));
  assert(!local_ok("https://127.0.0.1:8081/v1/chat/completions"));
  assert(!local_ok("http://192.168.4.100:8081/v1/chat/completions"));
  assert(!local_ok("http://127.0.0.1/v1/chat/completions"));
  assert(!local_ok("http://127.0.0.1:99999/v1/chat/completions"));
  assert(!local_ok("http://127.0.0.1:8081/v2/chat/completions"));
  assert(!local_ok("http://127.0.0.1:8081/v1extra"));
  assert(!local_ok("http://user@127.0.0.1:8081/v1/chat/completions"));
  assert(!local_ok("http://127.0.0.1:8081/chat/completions"));
}

}  // namespace

int main() {
  local_model_endpoint_test();
  return 0;
}
