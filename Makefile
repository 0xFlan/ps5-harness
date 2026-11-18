UNAME_S := $(shell uname -s)
CXX ?= c++
CC ?= cc
CXXFLAGS ?= -std=c++20 -O2 -Wall -Wextra -Iinclude
VERSION := $(shell cat VERSION)

# P2 package check. The SFO and icon come from host-only Python generators;
# eboot.bin and the ELF need the payload SDK. Require them whenever a console
# build is on disk, otherwise validate the two generated assets so a host-only
# checkout (CI) still tests clean.
PS5_TITLE_ID     := $(shell sed -n 's/^TITLE_ID[[:space:]]*:=[[:space:]]*//p' platform/ps5/title.mk)
PS5_PACKAGE      := .deps/ps5-build/$(PS5_TITLE_ID)
PS5_PACKAGE_MODE := $(if $(wildcard $(PS5_PACKAGE)/eboot.bin),,--assets-only)

ifeq ($(UNAME_S),Linux)
PLATFORM_LIBS := -lcurl -ldl -pthread
else
PLATFORM_LIBS := -lcurl
endif

LIB_SRC := $(filter-out src/main.cpp,$(wildcard src/*.cpp))
LIB_OBJ := $(patsubst src/%.cpp,build/%.o,$(LIB_SRC))

all: build/harness

build:
	@mkdir -p build

build/%.o: src/%.cpp | build
	$(CXX) $(CXXFLAGS) -c $< -o $@

build/harness: $(LIB_OBJ) build/main.o
	$(CXX) $(CXXFLAGS) $^ -o $@ $(PLATFORM_LIBS)

build/core_test.o: tests/core_test.cpp | build
	$(CXX) $(CXXFLAGS) -c $< -o $@

build/core_test: $(LIB_OBJ) build/core_test.o
	$(CXX) $(CXXFLAGS) $^ -o $@ $(PLATFORM_LIBS)

build/resources_test.o: tests/resources_test.cpp | build
	$(CXX) $(CXXFLAGS) -c $< -o $@

build/resources_test: $(LIB_OBJ) build/resources_test.o
	$(CXX) $(CXXFLAGS) $^ -o $@ $(PLATFORM_LIBS)

build/runtime_test.o: tests/runtime_test.cpp | build
	$(CXX) $(CXXFLAGS) -c $< -o $@

build/runtime_test: $(LIB_OBJ) build/runtime_test.o
	$(CXX) $(CXXFLAGS) $^ -o $@ $(PLATFORM_LIBS)

build/hardening_test.o: tests/hardening_test.cpp | build
	$(CXX) $(CXXFLAGS) -c $< -o $@

build/hardening_test: $(LIB_OBJ) build/hardening_test.o
	$(CXX) $(CXXFLAGS) $^ -o $@ $(PLATFORM_LIBS)

build/agents_test.o: tests/agents_test.cpp | build
	$(CXX) $(CXXFLAGS) -c $< -o $@

build/agents_test: $(LIB_OBJ) build/agents_test.o
	$(CXX) $(CXXFLAGS) $^ -o $@ $(PLATFORM_LIBS)

# The provider round-trip test needs the cleartext-loopback policy, which only
# exists in builds defining PS5H_TEST_HOOKS. Production targets never set it.
E2E_DEFS := -DPS5H_TEST_HOOKS

build/harness_e2e: $(LIB_SRC) src/main.cpp | build
	$(CXX) $(CXXFLAGS) $(E2E_DEFS) $^ -o $@ $(PLATFORM_LIBS)

build/e2e_agent_test: tests/e2e_agent_test.cpp $(LIB_SRC) | build
	$(CXX) $(CXXFLAGS) $(E2E_DEFS) $^ -o $@ $(PLATFORM_LIBS)

build/policy_surface_test: tests/policy_surface_test.cpp src/policy.cpp | build
	$(CXX) $(CXXFLAGS) $^ -o $@

build/ps5_package_test: tests/ps5_package_test.cpp | build
	$(CXX) $(CXXFLAGS) $< -o $@

test: build/harness build/harness_e2e build/policy_surface_test build/core_test build/resources_test build/runtime_test build/hardening_test build/agents_test build/e2e_agent_test build/ps5_package_test
	./build/policy_surface_test
	./build/core_test
	./build/resources_test
	./build/runtime_test
	./build/hardening_test
	./build/agents_test
	@$(MAKE) -C platform/ps5 host-assets
	./build/ps5_package_test $(PS5_PACKAGE) $(PS5_PACKAGE_MODE)
	./build/e2e_agent_test

tools: build/harness
	./build/harness --tools --endpoints examples/endpoints

examples/plugins/hello/hello.so: examples/plugins/hello/hello.c | build
	$(CC) -std=c11 -O2 -shared -fPIC -Iinclude $< -o $@

plugin-example: examples/plugins/hello/hello.so

# Fuzzing. The HTTP backend is stubbed so fuzzing never touches a socket, and
# manifests are generated with a fixed public base URL so policy checks cannot
# trigger DNS.
#
# Apple's clang ships no libFuzzer runtime, so the default targets use a
# deterministic ASan+UBSan driver. `make fuzz-libfuzzer` builds the real
# coverage-guided targets where the runtime exists (Linux/LLVM).
FUZZ_FLAGS := -std=c++20 -g -O1 -fsanitize=address,undefined -fno-omit-frame-pointer -Iinclude -DPS5H_HTTP_STUB
FUZZ_SRCS := src/json.cpp src/core.cpp src/endpoints.cpp src/policy.cpp src/http.cpp

build/fuzz_json_driver: fuzz/driver.cpp fuzz/fuzz_json.cpp $(FUZZ_SRCS) | build
	$(CXX) $(FUZZ_FLAGS) $^ -o $@

build/fuzz_manifest_driver: fuzz/driver.cpp fuzz/fuzz_manifest.cpp $(FUZZ_SRCS) | build
	$(CXX) $(FUZZ_FLAGS) $^ -o $@

fuzz: build/fuzz_json_driver build/fuzz_manifest_driver

fuzz-run: fuzz
	ASAN_OPTIONS=detect_leaks=0 ./build/fuzz_json_driver 20000
	ASAN_OPTIONS=detect_leaks=0 ./build/fuzz_manifest_driver 20000

LIBFUZZER_FLAGS := -std=c++20 -g -O1 -fsanitize=fuzzer,address -fno-omit-frame-pointer -Iinclude -DPS5H_HTTP_STUB

fuzz-libfuzzer: build/fuzz_json build/fuzz_manifest

build/fuzz_json: fuzz/fuzz_json.cpp src/json.cpp | build
	$(CXX) $(LIBFUZZER_FLAGS) $^ -o $@

build/fuzz_manifest: fuzz/fuzz_manifest.cpp $(FUZZ_SRCS) | build
	$(CXX) $(LIBFUZZER_FLAGS) $^ -o $@

clean:
	rm -rf build examples/plugins/hello/hello.so

.PHONY: all test tools plugin-example fuzz fuzz-run fuzz-libfuzzer clean
