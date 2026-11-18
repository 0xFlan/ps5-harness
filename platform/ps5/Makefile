# P2 console package build.
#
#   make -C platform/ps5               # full installable package (needs the SDK)
#   make -C platform/ps5 host-assets   # param.sfo + icon0.png only (no SDK)
#   make -C platform/ps5 clean
#
# The package lands in $(OUT)/$(TITLE_ID)/ and contains eboot.bin, bringup.elf,
# sce_sys/param.sfo and sce_sys/icon0.png. python3 is required for the SFO and
# icon generators; the payload SDK is required only for the binaries.
# Override the SDK location with PS5_PAYLOAD_SDK=/path/to/ps5-payload-sdk.

PS5_PAYLOAD_SDK ?= $(abspath ../../.deps/ps5-payload-sdk)
OUT             ?= $(abspath ../../.deps/ps5-build)
APP_VERSION     ?= 0x07590001
PYTHON          ?= python3
BUILD_ID        := $(shell cat ../../VERSION)-$(shell git -C ../.. rev-parse --short HEAD 2>/dev/null || echo nogit)
HAVE_SDK        := $(wildcard $(PS5_PAYLOAD_SDK)/toolchain/prospero.mk)

include title.mk

ifneq ($(HAVE_SDK),)
include $(PS5_PAYLOAD_SDK)/toolchain/prospero.mk
endif

ELF2SELF  := $(PS5_PAYLOAD_SDK)/samples/install_app/make_fself.py
MAKE_SFO  := tools/make_sfo.py
MAKE_ICON := tools/make_icon.py

# The payload and the host harness share one resource probe so the numbers a
# console reports use the same code an operator can run with --resources.
RESOURCE_SRC := ../../src/resources.cpp
RESOURCE_HDR := ../../include/ps5h/resources.h

# make_fself.py accepts ET_EXEC (and SCE variants), not ET_DYN; the SDK's
# driver defaults to PIE on this target, so opt out at the linker.
CXXFLAGS += -std=c++20 -O2 -Wall -Wextra -Werror -Wl,--no-pie -I../../include \
            -DPS5H_BUILD_ID=\"$(BUILD_ID)\" -DPS5H_TITLE_ID=\"$(TITLE_ID)\"

PKG     := $(OUT)/$(TITLE_ID)
ELF     := $(OUT)/bringup.elf
BIN     := $(PKG)/eboot.bin
PKG_ELF := $(PKG)/bringup.elf
SFO     := $(PKG)/sce_sys/param.sfo
ICON    := $(PKG)/sce_sys/icon0.png

all: package

ifneq ($(HAVE_SDK),)
package: $(BIN) $(PKG_ELF) $(SFO) $(ICON)
else
package:
	@echo "ps5: payload SDK not found at $(PS5_PAYLOAD_SDK)" >&2
	@echo "ps5: run 'make -C platform/ps5 host-assets' or set PS5_PAYLOAD_SDK" >&2
	@exit 1
endif

host-assets: $(SFO) $(ICON)

$(OUT):
	@mkdir -p $(OUT)

$(ELF): bringup.cpp $(RESOURCE_SRC) $(RESOURCE_HDR) | $(OUT)
	$(CXX) $(CXXFLAGS) -o $@ bringup.cpp $(RESOURCE_SRC)

$(BIN): $(ELF) param.json | $(OUT)
	@mkdir -p $(dir $@)sce_sys
	$(ELF2SELF) --ptype fake --app-version $(APP_VERSION) --fw-version $(APP_VERSION) $< $@
	cp param.json $(dir $@)sce_sys/param.json

$(PKG_ELF): $(ELF)
	@mkdir -p $(PKG)
	cp $< $@

$(SFO): $(MAKE_SFO) title.mk
	@mkdir -p $(dir $@)
	$(PYTHON) $(MAKE_SFO) --title-id "$(TITLE_ID)" --title "$(TITLE)" \
		--content-id "$(CONTENT_ID)" --app-ver "$(APP_VER)" --output $@

$(ICON): $(MAKE_ICON)
	@mkdir -p $(dir $@)
	$(PYTHON) $(MAKE_ICON) --title-id "$(TITLE_ID)" --output $@

clean:
	rm -rf $(OUT)

.PHONY: all package host-assets clean
