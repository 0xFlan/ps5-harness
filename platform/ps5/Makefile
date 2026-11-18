# PS5 console package build.
#
#   make -C platform/ps5               # real agent package (SDK + PacBrew)
#   make -C platform/ps5 bringup       # P2 resource probe ELF, not packaged
#   make -C platform/ps5 host-assets   # param.sfo + icon0.png only (no SDK)
#   make -C platform/ps5 clean
#
# The package lands in $(OUT)/$(TITLE_ID)/ and contains eboot.bin built from the
# real agent sources, harness.elf, sce_sys/param.sfo and sce_sys/icon0.png.
#
# The agent needs static libcurl/OpenSSL for the payload SDK's libc. Those come
# from the pinned PacBrew image:
#
#   mkdir -p ../../.deps/pacbrew && cd ../../.deps/pacbrew
#   curl -fLO https://github.com/ps5-payload-dev/pacbrew-repo/releases/download/v0.41.1/ps5-payload-dev.tar.gz
#   sha256sum -c ...   # e811bc2e4676c5bb7f80a8fe928d2985dce599996a5ca6b54da3e72699287887
#   mkdir root && tar -xzf ps5-payload-dev.tar.gz -C root
#
# Override PS5_PAYLOAD_SDK or PACBREW_ROOT for another location.

PS5_PAYLOAD_SDK ?= $(abspath ../../.deps/ps5-payload-sdk)
PACBREW_ROOT    ?= $(abspath ../../.deps/pacbrew/root/opt/ps5-payload-sdk)
OUT             ?= $(abspath ../../.deps/ps5-build)
APP_VERSION     ?= 0x07590001
PYTHON          ?= python3
BUILD_ID        := $(shell cat ../../VERSION)-$(shell git -C ../.. rev-parse --short HEAD 2>/dev/null || echo nogit)
HAVE_SDK        := $(wildcard $(PS5_PAYLOAD_SDK)/toolchain/prospero.mk)
HAVE_PACBREW    := $(wildcard $(PACBREW_ROOT)/target/user/homebrew/lib/libcurl.a)

include title.mk

ifneq ($(HAVE_SDK),)
include $(PS5_PAYLOAD_SDK)/toolchain/prospero.mk
endif

ELF2SELF  := $(PS5_PAYLOAD_SDK)/samples/install_app/make_fself.py
MAKE_SFO  := tools/make_sfo.py
MAKE_ICON := tools/make_icon.py

RESOURCE_SRC := ../../src/resources.cpp
RESOURCE_HDR := ../../include/ps5h/resources.h
AGENT_SRC    := $(wildcard ../../src/*.cpp) console_compat.cpp
AGENT_HDR    := $(wildcard ../../include/ps5h/*.h)
PACBREW_INC  := $(PACBREW_ROOT)/target/user/homebrew/include
PACBREW_LIB  := $(PACBREW_ROOT)/target/user/homebrew/lib

# make_fself.py accepts ET_EXEC (and SCE variants), not ET_DYN; the SDK's
# driver defaults to PIE on this target, so opt out at the linker.
CXXFLAGS += -std=c++20 -O2 -Wall -Wextra -Werror -Wl,--no-pie -I../../include \
            -DPS5H_BUILD_ID=\"$(BUILD_ID)\" -DPS5H_TITLE_ID=\"$(TITLE_ID)\"
AGENT_CXXFLAGS := $(CXXFLAGS) -DPS5H_PS5_TARGET -I$(PACBREW_INC) -DCURL_STATICLIB
AGENT_LIBS     := -L$(PACBREW_LIB) -lcurl -lpsl -lssl -lcrypto -lzstd -lz -ldl -pthread

PKG      := $(OUT)/$(TITLE_ID)
AGENT    := $(OUT)/harness.elf
BRINGUP  := $(OUT)/bringup.elf
BIN      := $(PKG)/eboot.bin
PKG_ELF  := $(PKG)/harness.elf
SFO      := $(PKG)/sce_sys/param.sfo
ICON     := $(PKG)/sce_sys/icon0.png

all: package

ifeq ($(HAVE_SDK),)
package:
	@echo "ps5: payload SDK not found at $(PS5_PAYLOAD_SDK)" >&2
	@echo "ps5: run 'make -C platform/ps5 host-assets' or set PS5_PAYLOAD_SDK" >&2
	@exit 1
else ifeq ($(HAVE_PACBREW),)
package:
	@echo "ps5: PacBrew libcurl not found at $(PACBREW_LIB)/libcurl.a" >&2
	@echo "ps5: see the download/extract block at the top of this Makefile" >&2
	@exit 1
else
package: $(BIN) $(PKG_ELF) $(SFO) $(ICON)
endif

host-assets: $(SFO) $(ICON)

bringup: $(BRINGUP)

$(OUT):
	@mkdir -p $(OUT)

$(AGENT): $(AGENT_SRC) $(AGENT_HDR) | $(OUT)
	$(CXX) $(AGENT_CXXFLAGS) -Wl,--wrap=fcntl -o $@ $(AGENT_SRC) $(AGENT_LIBS)

$(BRINGUP): bringup.cpp $(RESOURCE_SRC) $(RESOURCE_HDR) | $(OUT)
	$(CXX) $(CXXFLAGS) -o $@ bringup.cpp $(RESOURCE_SRC)

$(BIN): $(AGENT) param.json | $(OUT)
	@mkdir -p $(dir $@)sce_sys
	$(ELF2SELF) --ptype fake --app-version $(APP_VERSION) --fw-version $(APP_VERSION) $< $@
	cp param.json $(dir $@)sce_sys/param.json

$(PKG_ELF): $(AGENT)
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

.PHONY: all package host-assets bringup clean
