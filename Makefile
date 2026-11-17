UNAME_S := $(shell uname -s)
CXX ?= c++
CC ?= cc
CXXFLAGS ?= -std=c++20 -O2 -Wall -Wextra -Iinclude
VERSION := $(shell cat VERSION)

ifeq ($(UNAME_S),Linux)
PLATFORM_LIBS := -lcurl -ldl
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

build/hardening_test.o: tests/hardening_test.cpp | build
	$(CXX) $(CXXFLAGS) -c $< -o $@

build/hardening_test: $(LIB_OBJ) build/hardening_test.o
	$(CXX) $(CXXFLAGS) $^ -o $@ $(PLATFORM_LIBS)

test: build/core_test build/hardening_test
	./build/core_test
	./build/hardening_test

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
