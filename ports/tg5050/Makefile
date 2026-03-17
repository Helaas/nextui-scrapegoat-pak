SHELL := /bin/bash

CROSS_COMPILE ?= aarch64-nextui-linux-gnu-
CC := $(CROSS_COMPILE)gcc
STRIP := $(CROSS_COMPILE)strip
SRC := $(shell find /workspace/src /workspace/third_party/cJSON /workspace/third_party/md5 -name '*.c' -print | sort)
INCLUDES := -I/workspace/third_party/apostrophe/include -I/workspace/third_party/cJSON -I/workspace/third_party/md5
PKG_CONFIG := pkg-config
SDL_CFLAGS := $(shell $(PKG_CONFIG) --cflags sdl2)
SDL_LDFLAGS := $(shell $(PKG_CONFIG) --libs sdl2) -lSDL2_ttf -lSDL2_image

CFLAGS := -std=gnu11 -O2 -Wall -Wextra -Wno-unused-parameter \
	-mcpu=cortex-a55 -mtune=cortex-a55 \
	-DPLATFORM_TG5050 \
	$(CREDENTIAL_DEFINES) \
	-ffunction-sections -fdata-sections \
	$(INCLUDES) $(SDL_CFLAGS)

LDFLAGS := $(SDL_LDFLAGS) -lcurl -Wl,--gc-sections -lpthread -lm

all: $(BUILD_DIR)/scrapegoat

$(BUILD_DIR)/scrapegoat: $(SRC)
	@mkdir -p $(BUILD_DIR)
	$(CC) $(CFLAGS) -o $@ $(SRC) $(LDFLAGS)
	$(STRIP) $@
