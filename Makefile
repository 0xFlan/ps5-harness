UNAME_S := $(shell uname -s)
CXX ?= c++
CC ?= cc
OPENSSL_CFLAGS := $(shell pkg-config --cflags openssl 2>/dev/null)
OPENSSL_LIBS := $(shell pkg-config --libs openssl 2>/dev/null)
ifeq ($(OPENSSL_LIBS),)
OPENSSL_PREFIX := $(shell brew --prefix openssl 2>/dev/null)
OPENSSL_CFLAGS := $(if $(OPENSSL_PREFIX),-I$(OPENSSL_PREFIX)/include,)
OPENSSL_LIBS := $(if $(OPENSSL_PREFIX),-L$(OPENSSL_PREFIX)/lib -lssl -lcrypto,-lssl -lcrypto)
endif
CXXFLAGS ?= -std=c++20 -O2 -Wall -Wextra -Iinclude $(OPENSSL_CFLAGS)
VERSION := $(shell cat VERSION)

# P2 package check. The SFO and icon come from host-only Python generators;
# eboot.bin and the ELF need the payload SDK. Require them whenever a console
# build is on disk, otherwise validate the two generated assets so a host-only
# checkout (CI) still tests clean.
PS5_TITLE_ID     := $(shell sed -n 's/^TITLE_ID[[:space:]]*:=[[:space:]]*//p' platform/ps5/title.mk)
PS5_PACKAGE      := .deps/ps5-build/$(PS5_TITLE_ID)
PS5_PACKAGE_MODE := $(if $(wildcard $(PS5_PACKAGE)/eboot.bin),,--assets-only)

ifeq ($(UNAME_S),Linux)
PLATFORM_LIBS := -lcurl $(OPENSSL_LIBS) -ldl -pthread
else
PLATFORM_LIBS := -lcurl $(OPENSSL_LIBS)
endif

# The local-model runtime is llama.cpp linked into this binary. It is kept out
# of the unit-test objects (which do not need a 500 MiB model or its server).
LLAMA_HOST ?= $(abspath .deps/llama-host)
ifeq ($(UNAME_S),Linux)
LLAMA_FRAMEWORKS :=
else
LLAMA_FRAMEWORKS := -framework Accelerate -framework Foundation -framework Metal -framework MetalKit -framework CoreFoundation -framework Security
endif
LLAMA_LIBS := \
  $(LLAMA_HOST)/tools/server/libllama-server-impl.a \
  $(LLAMA_HOST)/tools/server/libserver-context.a \
  $(LLAMA_HOST)/common/libllama-common.a \
  $(LLAMA_HOST)/common/libllama-common-base.a \
  $(LLAMA_HOST)/tools/mtmd/libmtmd.a \
  $(LLAMA_HOST)/src/libllama.a \
  $(LLAMA_HOST)/ggml/src/libggml.a \
  $(LLAMA_HOST)/ggml/src/libggml-cpu.a \
  $(LLAMA_HOST)/ggml/src/ggml-blas/libggml-blas.a \
  $(LLAMA_HOST)/ggml/src/ggml-metal/libggml-metal.a \
  $(LLAMA_HOST)/ggml/src/libggml-base.a \
  $(LLAMA_HOST)/vendor/hash/libvendor-hash.a \
  $(LLAMA_HOST)/tools/ui/libllama-ui.a \
  $(LLAMA_HOST)/vendor/cpp-httplib/libcpp-httplib.a \
  -lm $(LLAMA_FRAMEWORKS)

LIB_SRC := $(filter-out src/main.cpp src/local_model.cpp,$(wildcard src/*.cpp))
LIB_OBJ := $(patsubst src/%.cpp,build/%.o,$(LIB_SRC))
LOCAL_MODEL_OBJ := build/local_model.o

all: build/harness

build:
	@mkdir -p build

build/%.o: src/%.cpp | build
	$(CXX) $(CXXFLAGS) -c $< -o $@

build/harness: $(LIB_OBJ) $(LOCAL_MODEL_OBJ) build/main.o
	$(CXX) $(CXXFLAGS) $^ -o $@ $(LLAMA_LIBS) $(PLATFORM_LIBS)

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

build/skills_test.o: tests/skills_test.cpp | build
	$(CXX) $(CXXFLAGS) -c $< -o $@

build/skills_test: $(LIB_OBJ) build/skills_test.o
	$(CXX) $(CXXFLAGS) $^ -o $@ $(PLATFORM_LIBS)

build/recon_test.o: tests/recon_test.cpp | build
	$(CXX) $(CXXFLAGS) -c $< -o $@

build/recon_test: $(LIB_OBJ) build/recon_test.o
	$(CXX) $(CXXFLAGS) $^ -o $@ $(PLATFORM_LIBS)

build/mission_test.o: tests/mission_test.cpp | build
	$(CXX) $(CXXFLAGS) -c $< -o $@

build/mission_test: $(LIB_OBJ) build/mission_test.o
	$(CXX) $(CXXFLAGS) $^ -o $@ $(PLATFORM_LIBS)

build/session_test.o: tests/session_test.cpp | build
	$(CXX) $(CXXFLAGS) -c $< -o $@

build/session_test: $(LIB_OBJ) build/session_test.o
	$(CXX) $(CXXFLAGS) $^ -o $@ $(PLATFORM_LIBS)

build/power_tools_test.o: tests/power_tools_test.cpp | build
	$(CXX) $(CXXFLAGS) -c $< -o $@

build/power_tools_test: $(LIB_OBJ) build/power_tools_test.o
	$(CXX) $(CXXFLAGS) $^ -o $@ $(PLATFORM_LIBS)

build/console_ui_test.o: tests/console_ui_test.cpp | build
	$(CXX) $(CXXFLAGS) -c $< -o $@

build/console_ui_test: $(LIB_OBJ) build/console_ui_test.o
	$(CXX) $(CXXFLAGS) $^ -o $@ $(PLATFORM_LIBS)

# server_test needs the network-gate test hook in server.cpp. Compile a
# separate test object so the production listener remains free of test code.
SERVER_TEST_DEFS := -DPS5H_TEST_HOOKS
LIB_OBJ_NO_SERVER := $(filter-out build/server.o,$(LIB_OBJ))

build/server_test.o: tests/server_test.cpp | build
	$(CXX) $(CXXFLAGS) $(SERVER_TEST_DEFS) -c $< -o $@

build/server_test_server.o: src/server.cpp | build
	$(CXX) $(CXXFLAGS) $(SERVER_TEST_DEFS) -c $< -o $@

build/server_test: $(LIB_OBJ_NO_SERVER) build/server_test.o build/server_test_server.o
	$(CXX) $(CXXFLAGS) $^ -o $@ $(PLATFORM_LIBS)

build/home_tile_test.o: tests/home_tile_test.cpp | build
	$(CXX) $(CXXFLAGS) -c $< -o $@

build/home_tile_test: $(LIB_OBJ) build/home_tile_test.o
	$(CXX) $(CXXFLAGS) $^ -o $@ $(PLATFORM_LIBS)

# The provider round-trip test needs the cleartext-loopback policy, which only
# exists in builds defining PS5H_TEST_HOOKS. Production targets never set it.
E2E_DEFS := -DPS5H_TEST_HOOKS

build/harness_e2e: $(LIB_SRC) src/local_model.cpp src/main.cpp | build
	$(CXX) $(CXXFLAGS) $(E2E_DEFS) $^ -o $@ $(LLAMA_LIBS) $(PLATFORM_LIBS)

build/e2e_agent_test: tests/e2e_agent_test.cpp $(LIB_SRC) | build
	$(CXX) $(CXXFLAGS) $(E2E_DEFS) $^ -o $@ $(PLATFORM_LIBS)

build/policy_surface_test: tests/policy_surface_test.cpp src/policy.cpp | build
	$(CXX) $(CXXFLAGS) $^ -o $@

build/ps5_package_test: tests/ps5_package_test.cpp | build
	$(CXX) $(CXXFLAGS) $< -o $@

# Browser-engine check for the console reconnect path. Chrome is driven over
# the DevTools protocol with no npm dependency. This target runs whenever node
# is present; the test itself skips clearly if Chrome or openssl is absent.
reconnect-test: build/harness
	@if command -v node >/dev/null 2>&1; then \
		CHROME_BIN="$(RECONNECT_CHROME)" node tests/reconnect_browser_test.mjs; \
	else \
		echo "reconnect_browser_test: SKIP (node unavailable)"; \
	fi

# Build a self-hosted user bundle. The package is produced from this checkout;
# the bundle explicitly excludes .deps, build outputs, configs and secrets.
release:
	$(MAKE) -C platform/ps5 clean
	$(MAKE) -C platform/ps5 package
	python3 tools/make_release.py

# Build one zip that also contains the on-console GGUF. Set LOCAL_MODEL to the
# downloaded Qwen3.5-0.8B-Q4_K_M.gguf path.
LOCAL_MODEL ?= $(HOME)/Downloads/Qwen3.5-0.8B-Q4_K_M.gguf
local-kit:
	$(MAKE) -C platform/ps5 package
	python3 tools/make_release.py --model "$(LOCAL_MODEL)"

test: build/harness build/harness_e2e build/policy_surface_test build/core_test build/resources_test build/runtime_test build/hardening_test build/agents_test build/skills_test build/recon_test build/mission_test build/session_test build/power_tools_test build/console_ui_test build/server_test build/home_tile_test build/e2e_agent_test build/ps5_package_test
	./build/policy_surface_test
	./build/core_test
	./build/resources_test
	./build/runtime_test
	./build/hardening_test
	./build/agents_test
	./build/skills_test
	./build/recon_test
	./build/mission_test
	./build/session_test
	./build/power_tools_test
	./build/console_ui_test
	./build/server_test
	./build/home_tile_test
	@$(MAKE) reconnect-test
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
FUZZ_RECON_SRCS := src/recon.cpp src/paths.cpp src/sha256.cpp src/core.cpp src/json.cpp
FUZZ_MISSION_SRCS := src/mission_parse.cpp src/json.cpp
FUZZ_LIBS := $(if $(filter Linux,$(UNAME_S)),-pthread,)
FUZZ_ASAN_OPTIONS := detect_leaks=1:halt_on_error=1
ifeq ($(UNAME_S),Darwin)
# Apple's AddressSanitizer has no LeakSanitizer. `make leaks-check` uses the
# system leaks tool on plain builds instead.
FUZZ_ASAN_OPTIONS := detect_leaks=0:halt_on_error=1
endif

build/fuzz_json_driver: fuzz/driver.cpp fuzz/fuzz_json.cpp $(FUZZ_SRCS) | build
	$(CXX) $(FUZZ_FLAGS) $^ -o $@

build/fuzz_manifest_driver: fuzz/driver.cpp fuzz/fuzz_manifest.cpp $(FUZZ_SRCS) | build
	$(CXX) $(FUZZ_FLAGS) $^ -o $@

build/fuzz_recon_driver: fuzz/driver.cpp fuzz/fuzz_recon.cpp $(FUZZ_RECON_SRCS) | build
	$(CXX) $(FUZZ_FLAGS) $^ -o $@ $(FUZZ_LIBS)

build/fuzz_mission_driver: fuzz/driver.cpp fuzz/fuzz_mission.cpp $(FUZZ_MISSION_SRCS) | build
	$(CXX) $(FUZZ_FLAGS) $^ -o $@

fuzz: build/fuzz_json_driver build/fuzz_manifest_driver build/fuzz_recon_driver build/fuzz_mission_driver

fuzz-run: fuzz
	ASAN_OPTIONS=$(FUZZ_ASAN_OPTIONS) UBSAN_OPTIONS=halt_on_error=1:print_stacktrace=1 ./build/fuzz_json_driver 20000
	ASAN_OPTIONS=$(FUZZ_ASAN_OPTIONS) UBSAN_OPTIONS=halt_on_error=1:print_stacktrace=1 ./build/fuzz_manifest_driver 20000
	ASAN_OPTIONS=$(FUZZ_ASAN_OPTIONS) UBSAN_OPTIONS=halt_on_error=1:print_stacktrace=1 ./build/fuzz_recon_driver 20000
	ASAN_OPTIONS=$(FUZZ_ASAN_OPTIONS) UBSAN_OPTIONS=halt_on_error=1:print_stacktrace=1 ./build/fuzz_mission_driver 20000

LIBFUZZER_FLAGS := -std=c++20 -g -O1 -fsanitize=fuzzer,address -fno-omit-frame-pointer -Iinclude -DPS5H_HTTP_STUB

fuzz-libfuzzer: build/fuzz_json build/fuzz_manifest build/fuzz_recon build/fuzz_mission

build/fuzz_json: fuzz/fuzz_json.cpp src/json.cpp | build
	$(CXX) $(LIBFUZZER_FLAGS) $^ -o $@

build/fuzz_manifest: fuzz/fuzz_manifest.cpp $(FUZZ_SRCS) | build
	$(CXX) $(LIBFUZZER_FLAGS) $^ -o $@

build/fuzz_recon: fuzz/fuzz_recon.cpp $(FUZZ_RECON_SRCS) | build
	$(CXX) $(LIBFUZZER_FLAGS) $^ -o $@ $(FUZZ_LIBS)

build/fuzz_mission: fuzz/fuzz_mission.cpp $(FUZZ_MISSION_SRCS) | build
	$(CXX) $(LIBFUZZER_FLAGS) $^ -o $@

# Plain -O1 builds for the system leaks tool on macOS. ASan's allocator and
# leak checker cannot coexist there, so this covers the leak leg separately.
PLAIN_FUZZ_FLAGS := -std=c++20 -g -O1 -Iinclude -DPS5H_HTTP_STUB

build/fuzz_recon_plain: fuzz/driver.cpp fuzz/fuzz_recon.cpp $(FUZZ_RECON_SRCS) | build
	$(CXX) $(PLAIN_FUZZ_FLAGS) $^ -o $@ $(FUZZ_LIBS)

build/fuzz_mission_plain: fuzz/driver.cpp fuzz/fuzz_mission.cpp $(FUZZ_MISSION_SRCS) | build
	$(CXX) $(PLAIN_FUZZ_FLAGS) $^ -o $@

# Deterministic host tests under the same sanitizers as the fuzz drivers. The
# tests cover path containment and mission execution as well as the parsers.
SAN_FLAGS := -std=c++20 -g -O1 -fsanitize=address,undefined -fno-omit-frame-pointer -Iinclude
SAN_ASAN_OPTIONS := detect_leaks=1:halt_on_error=1
ifeq ($(UNAME_S),Darwin)
SAN_ASAN_OPTIONS := detect_leaks=0:halt_on_error=1
endif

build/recon_test_san: tests/recon_test.cpp $(LIB_SRC) | build
	$(CXX) $(SAN_FLAGS) $^ -o $@ $(PLATFORM_LIBS)

build/mission_test_san: tests/mission_test.cpp $(LIB_SRC) | build
	$(CXX) $(SAN_FLAGS) $^ -o $@ $(PLATFORM_LIBS)

sanitize-test: build/recon_test_san build/mission_test_san
	ASAN_OPTIONS=$(SAN_ASAN_OPTIONS) UBSAN_OPTIONS=halt_on_error=1:print_stacktrace=1 ./build/recon_test_san
	ASAN_OPTIONS=$(SAN_ASAN_OPTIONS) UBSAN_OPTIONS=halt_on_error=1:print_stacktrace=1 ./build/mission_test_san

leaks-check: build/fuzz_recon_plain build/fuzz_mission_plain
ifeq ($(UNAME_S),Darwin)
	leaks -atExit -- ./build/fuzz_recon_plain 2000
	leaks -atExit -- ./build/fuzz_mission_plain 20000
else
	@echo "macOS leaks tool unavailable; use make fuzz-run for LeakSanitizer"
endif

clean:
	rm -rf build examples/plugins/hello/hello.so

.PHONY: all test release local-kit reconnect-test tools plugin-example fuzz fuzz-run fuzz-libfuzzer sanitize-test leaks-check clean
