SHELL := /bin/bash

CROSS_COMPILE ?= aarch64-nextui-linux-gnu-
CC := $(CROSS_COMPILE)gcc
STRIP := $(CROSS_COMPILE)strip
PLATFORM_ID := tg5050
SRC := $(shell find /workspace/src /workspace/third_party/cJSON /workspace/third_party/md5 /workspace/third_party/miniz -name '*.c' -print | sort)
INCLUDES := -I/workspace/third_party/apostrophe/include -I/workspace/third_party/cJSON -I/workspace/third_party/md5 -I/workspace/third_party/miniz
THIRD_PARTY_SCRIPT := /workspace/scripts/build_third_party.sh
PKG_CONFIG := pkg-config
SDL_CFLAGS := $(shell $(PKG_CONFIG) --cflags sdl2)
SDL_LDFLAGS := $(shell $(PKG_CONFIG) --libs sdl2) -lSDL2_ttf -lSDL2_image
CURL_PREFIX := /workspace/build/third_party/$(PLATFORM_ID)/curl/install
OPENSSL_PREFIX := /workspace/build/third_party/$(PLATFORM_ID)/openssl/install
OPENSSL_LIBSSL := $(OPENSSL_PREFIX)/lib/libssl.a
OPENSSL_LIBCRYPTO := $(OPENSSL_PREFIX)/lib/libcrypto.a
CURL_STAMP := $(CURL_PREFIX)/.stamp-curl-8.7.1-static2

CFLAGS := -std=gnu11 -O2 -Wall -Wextra -Wno-unused-parameter \
	-mcpu=cortex-a55 -mtune=cortex-a55 \
	-DPLATFORM_TG5050 \
	-DAP_ENABLE_CURL \
	$(CREDENTIAL_DEFINES) \
	-ffunction-sections -fdata-sections \
	$(INCLUDES) $(SDL_CFLAGS) -I$(CURL_PREFIX)/include

LDFLAGS := $(SDL_LDFLAGS) \
	-Wl,--whole-archive $(CURL_PREFIX)/lib/libcurl.a -Wl,--no-whole-archive \
	$(OPENSSL_LIBSSL) $(OPENSSL_LIBCRYPTO) \
	-lz -Wl,--gc-sections -lpthread -ldl -lm

all: $(BUILD_DIR)/scrapegoat

$(CURL_STAMP): /workspace/scripts/build_third_party.sh
	$(THIRD_PARTY_SCRIPT) ensure-curl $(PLATFORM_ID)

$(BUILD_DIR)/scrapegoat: $(SRC) $(CURL_STAMP)
	@mkdir -p $(BUILD_DIR)
	$(CC) $(CFLAGS) -o $@ $(SRC) $(LDFLAGS)
	$(STRIP) $@
	$(THIRD_PARTY_SCRIPT) stage-runtime-libs $(PLATFORM_ID) "$@" "$(BUILD_DIR)/lib"
