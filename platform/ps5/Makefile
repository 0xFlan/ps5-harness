# P2 console cross-build.
#
#   make -C platform/ps5            # bringup.elf + PPSA99177/eboot.bin
#   make -C platform/ps5 clean
#
# Requires the payload SDK (macOS or Linux host) and python3 for make_fself.py.
# Override the SDK location with PS5_PAYLOAD_SDK=/path/to/ps5-payload-sdk.

PS5_PAYLOAD_SDK ?= $(abspath ../../.deps/ps5-payload-sdk)
OUT             ?= $(abspath ../../.deps/ps5-build)
TITLE_ID        ?= PPSA99177
APP_VERSION     ?= 0x07590001
BUILD_ID        := $(shell cat ../../VERSION)-$(shell git -C ../.. rev-parse --short HEAD 2>/dev/null || echo nogit)

include $(PS5_PAYLOAD_SDK)/toolchain/prospero.mk

ELF2SELF := $(PS5_PAYLOAD_SDK)/samples/install_app/make_fself.py

# make_fself.py accepts ET_EXEC (and SCE variants), not ET_DYN; the SDK's
# driver defaults to PIE on this target, so opt out at the linker.
CXXFLAGS += -std=c++20 -O2 -Wall -Wextra -Werror -Wl,--no-pie -DPS5H_BUILD_ID=\"$(BUILD_ID)\"

ELF := $(OUT)/bringup.elf
BIN := $(OUT)/$(TITLE_ID)/eboot.bin

all: $(BIN)

$(OUT):
	@mkdir -p $(OUT)

$(ELF): bringup.cpp | $(OUT)
	$(CXX) $(CXXFLAGS) -o $@ $<

$(BIN): $(ELF) param.json
	@mkdir -p $(dir $@)sce_sys
	$(ELF2SELF) --ptype fake --app-version $(APP_VERSION) --fw-version $(APP_VERSION) $< $@
	cp param.json $(dir $@)sce_sys/param.json

clean:
	rm -rf $(OUT)

.PHONY: all clean
