// Compile-time proof that the test-only loopback policy does not exist in a
// production build. Built WITHOUT -DPS5H_TEST_HOOKS; if the field were ever
// compiled in unconditionally, this file would fail to compile.

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

}  // namespace

int main() { return 0; }
