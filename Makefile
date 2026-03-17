# ──────────────────────────────────────────────────────────────
# ScrapeGoat Pak — Build System (C / Apostrophe)
# ──────────────────────────────────────────────────────────────

SHELL := /bin/bash

APP_NAME := scrapegoat
PAK_NAME := ScrapeGoat
APOSTROPHE_DIR := third_party/apostrophe
BUILD_DIR := build
DIST_DIR := $(BUILD_DIR)/release
STAGING_DIR := $(BUILD_DIR)/staging
CACHE_DIR := .cache
GIT_STATIC_CACHE := $(CACHE_DIR)/git-static

GIT_VERSION      := 2.53.0
CURL_VERSION     := 8.11.1

SRC_FILES := $(shell find src third_party/cJSON third_party/md5 third_party/miniz -name '*.c' -print 2>/dev/null | sort)

TG5040_TOOLCHAIN := ghcr.io/loveretro/tg5040-toolchain:latest
TG5050_TOOLCHAIN := ghcr.io/loveretro/tg5050-toolchain:latest
MY355_TOOLCHAIN  := ghcr.io/loveretro/my355-toolchain:latest
ADB ?= adb

COMMON_INCLUDES := -I$(APOSTROPHE_DIR)/include -Ithird_party/cJSON -Ithird_party/md5 -Ithird_party/miniz

# ── Credential validation ─────────────────────────────────────
-include .env.local

CREDENTIAL_DEFINES :=
ifdef SCREENSCRAPER_DEV_ID
CREDENTIAL_DEFINES += -DSCREENSCRAPER_DEV_ID=\"$(SCREENSCRAPER_DEV_ID)\"
endif
ifdef SCREENSCRAPER_DEV_PASSWORD
CREDENTIAL_DEFINES += -DSCREENSCRAPER_DEV_PASSWORD=\"$(SCREENSCRAPER_DEV_PASSWORD)\"
endif
ifdef SCREENSCRAPER_DEBUG_PASSWORD
CREDENTIAL_DEFINES += -DSCREENSCRAPER_DEBUG_PASSWORD=\"$(SCREENSCRAPER_DEBUG_PASSWORD)\"
endif
ifdef SCREENSCRAPER_FORCE_LEVEL
CREDENTIAL_DEFINES += -DSCREENSCRAPER_FORCE_LEVEL=\"$(SCREENSCRAPER_FORCE_LEVEL)\"
endif
ifdef SCREENSCRAPER_FORCE_UPDATE
CREDENTIAL_DEFINES += -DSCREENSCRAPER_FORCE_UPDATE=\"$(SCREENSCRAPER_FORCE_UPDATE)\"
endif

.PHONY: all native mac run-mac run-native tg5040 tg5050 my355 \
	package package-tg5040 package-tg5050 package-my355 do-package \
	deploy deploy-platform clean clean-all help check-credentials \
	build-git-static clean-git-static

# ── Default target ──────────────────────────────────────────

native: mac
run-native: run-mac
all: tg5040 tg5050 my355

# ── Submodule auto-init ────────────────────────────────────

$(APOSTROPHE_DIR)/include/apostrophe.h:
	git submodule update --init

# ── Credential checking ────────────────────────────────────

check-credentials:
	@if [ ! -f .env.local ]; then \
		echo "ERROR: .env.local not found"; \
		echo "Please copy .env.example to .env.local and set your credentials from screenscraper.fr"; \
		exit 1; \
	fi
	@if [ -z "$(SCREENSCRAPER_DEV_ID)" ] || [ -z "$(SCREENSCRAPER_DEV_PASSWORD)" ]; then \
		echo "ERROR: SCREENSCRAPER_DEV_ID and SCREENSCRAPER_DEV_PASSWORD must be set in .env.local"; \
		exit 1; \
	fi
	@echo "✓ Credentials loaded from .env.local"

# ── Native macOS build ──────────────────────────────────────

mac: check-credentials $(APOSTROPHE_DIR)/include/apostrophe.h
	@mkdir -p $(BUILD_DIR)/mac
	cc -std=gnu11 -O0 -g \
		-DPLATFORM_MAC \
		$(CREDENTIAL_DEFINES) \
		$(COMMON_INCLUDES) \
		$(shell pkg-config --cflags sdl2 SDL2_ttf SDL2_image libcurl) \
		-o $(BUILD_DIR)/mac/$(APP_NAME) \
		$(SRC_FILES) \
		$(shell pkg-config --libs sdl2 SDL2_ttf SDL2_image libcurl) \
		-lm -lpthread

run-mac: mac
	./$(BUILD_DIR)/mac/$(APP_NAME)

# ── Docker cross-compilation ────────────────────────────────

tg5040: check-credentials $(APOSTROPHE_DIR)/include/apostrophe.h
	@mkdir -p $(BUILD_DIR)/tg5040
	docker run --rm \
		-v "$(CURDIR)":/workspace \
		-e CREDENTIAL_DEFINES='$(CREDENTIAL_DEFINES)' \
		$(TG5040_TOOLCHAIN) \
		make -C /workspace -f ports/tg5040/Makefile \
			BUILD_DIR=/workspace/$(BUILD_DIR)/tg5040

tg5050: check-credentials $(APOSTROPHE_DIR)/include/apostrophe.h
	@mkdir -p $(BUILD_DIR)/tg5050
	docker run --rm \
		-v "$(CURDIR)":/workspace \
		-e CREDENTIAL_DEFINES='$(CREDENTIAL_DEFINES)' \
		$(TG5050_TOOLCHAIN) \
		make -C /workspace -f ports/tg5050/Makefile \
			BUILD_DIR=/workspace/$(BUILD_DIR)/tg5050

my355: check-credentials $(APOSTROPHE_DIR)/include/apostrophe.h
	@mkdir -p $(BUILD_DIR)/my355
	docker run --rm \
		-v "$(CURDIR)":/workspace \
		-e CREDENTIAL_DEFINES='$(CREDENTIAL_DEFINES)' \
		$(MY355_TOOLCHAIN) \
		make -C /workspace -f ports/my355/Makefile \
			BUILD_DIR=/workspace/$(BUILD_DIR)/my355

# ── Static git build (Alpine + musl, cached) ─────────────────

build-git-static: $(GIT_STATIC_CACHE)/git

$(GIT_STATIC_CACHE)/git:
	@mkdir -p $(GIT_STATIC_CACHE)
	docker run --rm --platform linux/arm64 \
		-v "$(CURDIR)":/build \
		-v "$(CURDIR)/$(GIT_STATIC_CACHE)":/out \
		-e GIT_VERSION=$(GIT_VERSION) \
		-e CURL_VERSION=$(CURL_VERSION) \
		alpine:3.21 \
		sh /build/scripts/build-git-static.sh

clean-git-static:
	rm -rf $(GIT_STATIC_CACHE)

# ── Packaging ───────────────────────────────────────────────

package-tg5040: tg5040 $(GIT_STATIC_CACHE)/git
	@$(MAKE) do-package PLATFORM=tg5040

package-tg5050: tg5050 $(GIT_STATIC_CACHE)/git
	@$(MAKE) do-package PLATFORM=tg5050

package-my355: my355 $(GIT_STATIC_CACHE)/git
	@$(MAKE) do-package PLATFORM=my355

do-package:
	@rm -rf $(BUILD_DIR)/$(PLATFORM)/$(PAK_NAME).pak
	@mkdir -p $(BUILD_DIR)/$(PLATFORM)/$(PAK_NAME).pak/resources/bin
	@cp $(BUILD_DIR)/$(PLATFORM)/$(APP_NAME) $(BUILD_DIR)/$(PLATFORM)/$(PAK_NAME).pak/
	@cp launch.sh pak.json LICENSE $(BUILD_DIR)/$(PLATFORM)/$(PAK_NAME).pak/
	@cp $(GIT_STATIC_CACHE)/git $(BUILD_DIR)/$(PLATFORM)/$(PAK_NAME).pak/resources/bin/
	@cp $(GIT_STATIC_CACHE)/git-remote-https $(BUILD_DIR)/$(PLATFORM)/$(PAK_NAME).pak/resources/bin/ 2>/dev/null || true
	@if [ -d "$(BUILD_DIR)/$(PLATFORM)/lib" ]; then \
		mkdir -p "$(BUILD_DIR)/$(PLATFORM)/$(PAK_NAME).pak/lib"; \
		cp -a "$(BUILD_DIR)/$(PLATFORM)/lib/." "$(BUILD_DIR)/$(PLATFORM)/$(PAK_NAME).pak/lib/"; \
	fi
	@mkdir -p $(DIST_DIR)/$(PLATFORM)
	@rm -f $(DIST_DIR)/$(PLATFORM)/$(PAK_NAME).pak.zip
	@cd $(BUILD_DIR)/$(PLATFORM) && zip -r "$(CURDIR)/$(DIST_DIR)/$(PLATFORM)/$(PAK_NAME).pak.zip" "$(PAK_NAME).pak" -x '.*'

package: package-tg5040 package-tg5050 package-my355
	@rm -rf $(STAGING_DIR)
	@mkdir -p $(STAGING_DIR)/Tools/tg5040 $(STAGING_DIR)/Tools/tg5050 $(STAGING_DIR)/Tools/my355
	@cp -a $(BUILD_DIR)/tg5040/$(PAK_NAME).pak $(STAGING_DIR)/Tools/tg5040/
	@cp -a $(BUILD_DIR)/tg5050/$(PAK_NAME).pak $(STAGING_DIR)/Tools/tg5050/
	@cp -a $(BUILD_DIR)/my355/$(PAK_NAME).pak $(STAGING_DIR)/Tools/my355/
	@mkdir -p $(DIST_DIR)/all
	@rm -f $(DIST_DIR)/all/$(PAK_NAME).pakz
	@cd $(STAGING_DIR) && zip -9 -r "$(CURDIR)/$(DIST_DIR)/all/$(PAK_NAME).pakz" . -x '.*'

# ── ADB deploy ──────────────────────────────────────────────

deploy:
	@echo "Detecting platform..."
	@SERIAL="$(ADB_SERIAL)"; \
	if [ -z "$$SERIAL" ]; then \
		SERIAL=$$($(ADB) devices | awk 'NR>1 && $$2=="device" {print $$1; exit}'); \
	fi; \
	if [ -z "$$SERIAL" ]; then \
		echo "Error: No online adb device found."; \
		exit 1; \
	fi; \
	ADB_CMD="$(ADB) -s $$SERIAL"; \
	FINGERPRINT=$$($$ADB_CMD shell ' \
		cat /proc/device-tree/compatible 2>/dev/null; \
		echo; \
		cat /proc/device-tree/model 2>/dev/null; \
		echo; \
		uname -a 2>/dev/null' 2>/dev/null | tr '\000' '\n' | tr -d '\r'); \
	case "$$FINGERPRINT" in \
		*rk3566*|*miyoo-355*) PLATFORM=my355 ;; \
		*allwinner,a523*|*sun55iw3*) PLATFORM=tg5050 ;; \
		*allwinner,a133*|*sun50iw*) PLATFORM=tg5040 ;; \
		*allwinner*) \
			if printf '%s' "$$FINGERPRINT" | grep -qi 'a523'; then \
				PLATFORM=tg5050; \
			else \
				PLATFORM=tg5040; \
			fi \
			;; \
		*) \
			echo "Error: Could not detect a supported platform from adb fingerprint."; \
			echo "  Serial: $$SERIAL"; \
			echo "  Fingerprint snippet: $$(printf '%s' "$$FINGERPRINT" | head -c 240)"; \
			exit 1; \
			;; \
	esac; \
	echo "Detected adb serial: $$SERIAL"; \
	echo "Detected platform: $$PLATFORM"; \
	$(MAKE) deploy-platform PLATFORM=$$PLATFORM SERIAL=$$SERIAL

deploy-platform:
	@if [ -z "$(PLATFORM)" ] || [ -z "$(SERIAL)" ]; then \
		echo "Error: deploy-platform requires PLATFORM and SERIAL."; \
		exit 1; \
	fi
	@$(MAKE) package-$(PLATFORM)
	@ADB_CMD="$(ADB) -s $(SERIAL)"; \
	PAK_ROOT="/mnt/SDCARD/Tools/$(PLATFORM)"; \
	PAK_DIR="$$PAK_ROOT/$(PAK_NAME).pak"; \
	echo "Deploying $(PAK_NAME).pak to $$PAK_DIR..."; \
	$$ADB_CMD shell "rm -rf '$$PAK_DIR' && mkdir -p '$$PAK_ROOT'"; \
	$$ADB_CMD push "$(BUILD_DIR)/$(PLATFORM)/$(PAK_NAME).pak" "$$PAK_ROOT/"; \
	echo "Deploy complete."

# ── Cleanup ─────────────────────────────────────────────────

clean:
	rm -rf $(BUILD_DIR)

clean-all: clean
	rm -rf $(CACHE_DIR)

# ── Help ────────────────────────────────────────────────────

help:
	@echo "Targets:"
	@echo "  native        Build the mac development binary"
	@echo "  run-native    Build and run the mac binary"
	@echo "  all           Build tg5040, tg5050, and my355"
	@echo "  mac           Build for macOS (native)"
	@echo "  run-mac       Build and run for macOS"
	@echo "  tg5040        Build for TG5040 (Docker cross-compile)"
	@echo "  tg5050        Build for TG5050 (Docker cross-compile)"
	@echo "  my355         Build for Miyoo Flip (Docker cross-compile)"
	@echo "  package       Package all platforms (.pak.zip + .pakz)"
	@echo "  deploy        Detect adb platform, package, and push"
	@echo "  build-git-static  Build static git binary (cached)"
	@echo "  clean-git-static  Remove cached static git"
	@echo "  clean         Remove build artifacts"
	@echo "  clean-all     Remove build + cache"
