# ──────────────────────────────────────────────────────────────
# ScrapeGoat Pak — Build System
# ──────────────────────────────────────────────────────────────

APP_NAME   := scrapegoat
PAK_NAME   := ScrapeGoat
MODULE     := github.com/Helaas/nextui-scrapegoat-pak
GABAGOOL   := github.com/BrandonKowalski/gabagool/v2
DOCKER_TG5040 := ghcr.io/loveretro/tg5040-toolchain
DOCKER_TG5050 := ghcr.io/loveretro/tg5050-toolchain

BUILD_DIR        := build
CACHE_DIR        := .cache
TOOLCHAIN_CACHE  := $(CACHE_DIR)/go-toolchain
GIT_STATIC_CACHE := $(CACHE_DIR)/git-static
SDL2_GFX_CACHE   := $(CACHE_DIR)/sdl2-gfx

GIT_VERSION      := 2.53.0
CURL_VERSION     := 8.11.1
SDL2_GFX_VERSION := main

GO_VERSION   := 1.24.2
GO_SDK_CACHE := $(CACHE_DIR)/go-sdk

LDFLAGS = -X main.ssDevID=$(SCREENSCRAPER_DEV_ID) -X main.ssDevPwd=$(SCREENSCRAPER_DEV_PASSWORD) \
          -X main.ssDebugPwd=$(SCREENSCRAPER_DEBUG_PASSWORD) \
          -X main.ssForceLevel=$(SCREENSCRAPER_FORCE_LEVEL) \
          -X main.ssForceUpdate=$(SCREENSCRAPER_FORCE_UPDATE)

# ── Credential validation ─────────────────────────────────────
# Load environment variables from .env.local (for ScreenScraper credentials)
# This file must be created for build targets to succeed

-include .env.local

# ── Helper target for credential checking ─────────────────────

.PHONY: check-credentials
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
	@if [ ! -z "$(SCREENSCRAPER_FORCE_LEVEL)" ]; then \
		echo "ℹ Debug mode: forcelevel=$(SCREENSCRAPER_FORCE_LEVEL)"; \
	fi

# ── Debug helper ──────────────────────────────────────────────
# Shows threading information for different force levels

.PHONY: debug-levels
debug-levels:
	@echo "ScreenScraper API Force Levels & Thread Limits:"
	@echo ""
	@echo "For each debug user level, the API enforces different thread limits."
	@echo "Using forcelevel=X in debug mode tests the behavior at that level."
	@echo ""
	@echo "Examples (adjust SCREENSCRAPER_FORCE_LEVEL in .env.local):"
	@echo "  Level 30  → maxthreads=1   (very slow, sequential)"
	@echo "  Level 50  → maxthreads=2   (slow)"
	@echo "  Level 70  → maxthreads=5   (moderate)"
	@echo "  Level 80  → maxthreads=10  (fast)"
	@echo "  Level 90+ → maxthreads=20+ (fastest)"
	@echo ""
	@echo "See: https://www.screenscraper.fr/webapi2.php → userlevelsListe.php"
	@echo ""
	@echo "To test with more threads, edit .env.local:"
	@echo "  SCREENSCRAPER_FORCE_LEVEL=80"
	@echo "Then rebuild: make mac"

# ── Platform auto-detection ──────────────────────────────────

ifdef PLATFORM
ifeq ($(PLATFORM),tg5040)
all: tg5040
else ifeq ($(PLATFORM),tg5050)
all: tg5050
else
all: mac
endif
else ifeq ($(shell uname -s),Darwin)
all: mac
else
all: tg5040
endif

# ── Native macOS build ───────────────────────────────────────

mac: check-credentials
	@mkdir -p $(BUILD_DIR)
	CGO_ENABLED=1 go build -mod=vendor -ldflags '$(LDFLAGS)' -o $(BUILD_DIR)/$(APP_NAME) .

# ── Docker ARM64 builds ──────────────────────────────────────
# SDL2/SDL2_image/SDL2_ttf are provided by device firmware (sysroot .so stubs used at link time).
# SDL2_gfx is cross-compiled as a static library and linked directly into the binary.
# git is a statically compiled binary with no shared lib deps.
# No lib/ directory is needed — nothing is bundled.

SYSROOT := /opt/aarch64-nextui-linux-gnu/aarch64-nextui-linux-gnu/libc

define DOCKER_BUILD
	@mkdir -p $(BUILD_DIR)/$(1)/bin $(CACHE_DIR)/go-mod $(CACHE_DIR)/go-build $(TOOLCHAIN_CACHE)
	docker run --rm --platform linux/arm64 \
		-v "$(CURDIR)":/build \
		-v "$(CURDIR)/$(GO_SDK_CACHE)":/usr/local/go \
		-v "$(CURDIR)/$(CACHE_DIR)/go-mod":/root/go/pkg/mod \
		-v "$(CURDIR)/$(CACHE_DIR)/go-build":/root/.cache/go-build \
		-v "$(CURDIR)/$(TOOLCHAIN_CACHE)":/root/.cache/go-toolchain \
		-v "$(CURDIR)/$(SDL2_GFX_CACHE)":/sdl2-gfx \
		-w /build \
		$(2) \
		sh -c 'set -e && \
		       SYSROOT=$(SYSROOT) && \
		       cp /sdl2-gfx/libSDL2_gfx.a $$SYSROOT/usr/lib/ && \
		       cp /sdl2-gfx/include/*.h $$SYSROOT/usr/include/ && \
		       cp /sdl2-gfx/include/*.h $$SYSROOT/usr/include/SDL2/ && \
		       cp /sdl2-gfx/pkgconfig/*.pc $$SYSROOT/usr/lib/pkgconfig/ && \
		       PATH=/usr/local/go/bin:$$PATH \
		       GOTOOLCHAINCACHE=/root/.cache/go-toolchain \
		       CGO_ENABLED=1 \
		       CC=aarch64-nextui-linux-gnu-gcc \
		       PKG_CONFIG_SYSROOT_DIR=$$SYSROOT \
		       PKG_CONFIG_LIBDIR=$$SYSROOT/usr/lib/pkgconfig \
		       CGO_LDFLAGS="-lm" \
		       GOOS=linux GOARCH=arm64 \
		       go build -mod=vendor -ldflags "$(LDFLAGS)" -o $(BUILD_DIR)/$(1)/$(APP_NAME) . && \
		       sh /build/scripts/bundle-git.sh $(BUILD_DIR)/$(1)'
endef

tg5040: check-credentials $(GIT_STATIC_CACHE)/git $(GO_SDK_CACHE)/bin/go $(SDL2_GFX_CACHE)/libSDL2_gfx.a
	$(call DOCKER_BUILD,tg5040,$(DOCKER_TG5040))

tg5050: check-credentials $(GIT_STATIC_CACHE)/git $(GO_SDK_CACHE)/bin/go $(SDL2_GFX_CACHE)/libSDL2_gfx.a
	$(call DOCKER_BUILD,tg5050,$(DOCKER_TG5050))

embedded: tg5040 tg5050

# ── SDL2_gfx static library (cross-compiled, cached) ─────────
# Builds libSDL2_gfx.a using the tg5040 toolchain cross-compiler against the
# toolchain sysroot's SDL2 2.26.1. Also creates pkg-config .pc files for
# SDL2_gfx, SDL2_image, and SDL2_ttf (which are missing from the sysroot).
# Result is cached in .cache/sdl2-gfx/ — only rebuilt when missing.
# To force a rebuild: make clean-sdl2-gfx

build-sdl2-gfx: $(SDL2_GFX_CACHE)/libSDL2_gfx.a

$(SDL2_GFX_CACHE)/libSDL2_gfx.a:
	@mkdir -p $(SDL2_GFX_CACHE)/include $(SDL2_GFX_CACHE)/pkgconfig
	docker run --rm --platform linux/arm64 \
		-v "$(CURDIR)/$(SDL2_GFX_CACHE)":/out \
		$(DOCKER_TG5040) \
		sh -c 'set -e && \
		       SYSROOT=$(SYSROOT) && \
		       CC=aarch64-nextui-linux-gnu-gcc && \
		       AR=aarch64-nextui-linux-gnu-ar && \
		       wget -q "https://github.com/ferzkopp/SDL2_gfx/archive/refs/heads/$(SDL2_GFX_VERSION).tar.gz" \
		            -O /tmp/sdl2-gfx.tar.gz && \
		       cd /tmp && tar xf sdl2-gfx.tar.gz && cd SDL2_gfx-$(SDL2_GFX_VERSION) && \
		       $$CC -O2 \
		           -I$$SYSROOT/usr/include \
		           -I$$SYSROOT/usr/include/SDL2 \
		           -c SDL2_gfxPrimitives.c SDL2_imageFilter.c SDL2_rotozoom.c SDL2_framerate.c && \
		       $$AR rcs /out/libSDL2_gfx.a \
		           SDL2_gfxPrimitives.o SDL2_imageFilter.o SDL2_rotozoom.o SDL2_framerate.o && \
		       cp SDL2_framerate.h SDL2_gfxPrimitives.h SDL2_imageFilter.h SDL2_rotozoom.h /out/include/ && \
		       printf "prefix=/usr\nexec_prefix=\$${prefix}\nlibdir=\$${exec_prefix}/lib\nincludedir=\$${prefix}/include\n\nName: SDL2_gfx\nDescription: SDL2 graphics primitives\nVersion: 1.0.4\nLibs: -L\$${libdir} -lSDL2_gfx -lm\nCflags: -I\$${includedir} -I\$${includedir}/SDL2\n" > /out/pkgconfig/SDL2_gfx.pc && \
		       printf "prefix=/usr\nexec_prefix=\$${prefix}\nlibdir=\$${exec_prefix}/lib\nincludedir=\$${prefix}/include\n\nName: SDL2_image\nDescription: SDL2 image loading library\nVersion: 2.0.1\nLibs: -L\$${libdir} -lSDL2_image\nCflags: -I\$${includedir} -I\$${includedir}/SDL2\n" > /out/pkgconfig/SDL2_image.pc && \
		       printf "prefix=/usr\nexec_prefix=\$${prefix}\nlibdir=\$${exec_prefix}/lib\nincludedir=\$${prefix}/include\n\nName: SDL2_ttf\nDescription: SDL2 TrueType font rendering library\nVersion: 2.0.10\nLibs: -L\$${libdir} -lSDL2_ttf\nCflags: -I\$${includedir} -I\$${includedir}/SDL2\n" > /out/pkgconfig/SDL2_ttf.pc && \
		       echo "sdl2-gfx: build done. $(ls -lh /out/libSDL2_gfx.a)"'

clean-sdl2-gfx:
	rm -rf $(SDL2_GFX_CACHE)

# ── Static git build (Alpine + musl, cached) ─────────────────
# Builds a minimal static git binary with HTTPS support.
# Result is cached in .cache/git-static/ — only rebuilt when missing.
# To force a rebuild: make clean-git-static

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

# ── Go SDK download (ARM64, cached) ──────────────────────────
# Downloads the Go toolchain for use inside the loveretro toolchain images.
# Result is cached in .cache/go-sdk/ — only rebuilt when missing.
# To force a rebuild: make clean-go-sdk

build-go-sdk: $(GO_SDK_CACHE)/bin/go

$(GO_SDK_CACHE)/bin/go:
	@mkdir -p $(GO_SDK_CACHE)
	docker run --rm --platform linux/arm64 \
		-v "$(CURDIR)/$(GO_SDK_CACHE)":/go-sdk \
		alpine:3.21 \
		sh -c 'apk add --no-cache wget ca-certificates && \
		       wget -q "https://go.dev/dl/go$(GO_VERSION).linux-arm64.tar.gz" -O /tmp/go.tar.gz && \
		       tar -C /go-sdk --strip-components=1 -xf /tmp/go.tar.gz'

clean-go-sdk:
	rm -rf $(GO_SDK_CACHE)

# ── Vendor patches ────────────────────────────────────────────
# Gabagool hardcodes /dev/input/event1 for the power button.
# TG5050 uses /dev/input/event2. This target applies the fix.

GABAGOOL_INIT := vendor/github.com/BrandonKowalski/gabagool/v2/pkg/gabagool/init.go
GABAGOOL_NEXTVAL := vendor/github.com/BrandonKowalski/gabagool/v2/pkg/gabagool/platform/nextui/theming.go
CERTIFIABLE := vendor/github.com/BrandonKowalski/certifiable/certifiable.go

patch-vendor:
	@if [ -f "$(GABAGOOL_INIT)" ] && grep -q 'DevicePath:.*"/dev/input/event1"' "$(GABAGOOL_INIT)"; then \
		echo "Patching Gabagool power button for TG5050 support..."; \
		cp patches/gabagool-power-button-tg5050.patch /tmp/_gaba_patch.patch; \
		cd "$(CURDIR)" && git apply --whitespace=nowarn patches/gabagool-power-button-tg5050.patch 2>/dev/null || \
			patch -p1 < patches/gabagool-power-button-tg5050.patch; \
		echo "Patch applied."; \
	else \
		echo "Gabagool power button patch already applied (or vendor not present)."; \
	fi
	@if [ -f "$(GABAGOOL_NEXTVAL)" ] && ! grep -q 'platformEnv := strings.ToLower' "$(GABAGOOL_NEXTVAL)"; then \
		echo "Patching Gabagool nextval path for TG5050 support..."; \
		cp patches/gabagool-nextval-path-tg5050.patch /tmp/_gaba_nextval_patch.patch; \
		cd "$(CURDIR)" && git apply --whitespace=nowarn patches/gabagool-nextval-path-tg5050.patch 2>/dev/null || \
			patch -p1 < patches/gabagool-nextval-path-tg5050.patch; \
		echo "Patch applied."; \
	else \
		echo "Gabagool nextval path patch already applied (or vendor not present)."; \
	fi
	@if [ -f "$(CERTIFIABLE)" ] && ! grep -q 'func CACerts' "$(CERTIFIABLE)"; then \
		echo "Patching certifiable to export CA certs..."; \
		cd "$(CURDIR)" && git apply --whitespace=nowarn patches/certifiable-export-cacerts.patch 2>/dev/null || \
			patch -p1 < patches/certifiable-export-cacerts.patch; \
		echo "Patch applied."; \
	else \
		echo "Certifiable CACerts patch already applied (or vendor not present)."; \
	fi

# ── Dependency management ────────────────────────────────────

deps:
	go get $(GABAGOOL)@latest
	go mod tidy
	go mod vendor
	$(MAKE) patch-vendor

# ── Packaging ────────────────────────────────────────────────

package-tg5040: tg5040
	@rm -rf $(BUILD_DIR)/pak-stage
	@mkdir -p $(BUILD_DIR)/pak-stage/resources/bin
	@mkdir -p $(BUILD_DIR)/release/tg5040
	@rm -f $(BUILD_DIR)/release/tg5040/$(PAK_NAME).pak.zip
	cp $(BUILD_DIR)/tg5040/$(APP_NAME) $(BUILD_DIR)/pak-stage/
	cp launch.sh $(BUILD_DIR)/pak-stage/
	cp pak.json $(BUILD_DIR)/pak-stage/
	cp LICENSE $(BUILD_DIR)/pak-stage/ 2>/dev/null || true
	cp $(BUILD_DIR)/tg5040/bin/* $(BUILD_DIR)/pak-stage/resources/bin/
	cd $(BUILD_DIR)/pak-stage && zip -r "$(CURDIR)/$(BUILD_DIR)/release/tg5040/$(PAK_NAME).pak.zip" . -x '.*'
	@rm -rf $(BUILD_DIR)/pak-stage

package-tg5050: tg5050
	@rm -rf $(BUILD_DIR)/pak-stage
	@mkdir -p $(BUILD_DIR)/pak-stage/resources/bin
	@mkdir -p $(BUILD_DIR)/release/tg5050
	@rm -f $(BUILD_DIR)/release/tg5050/$(PAK_NAME).pak.zip
	cp $(BUILD_DIR)/tg5050/$(APP_NAME) $(BUILD_DIR)/pak-stage/
	cp launch.sh $(BUILD_DIR)/pak-stage/
	cp pak.json $(BUILD_DIR)/pak-stage/
	cp LICENSE $(BUILD_DIR)/pak-stage/ 2>/dev/null || true
	cp $(BUILD_DIR)/tg5050/bin/* $(BUILD_DIR)/pak-stage/resources/bin/
	cd $(BUILD_DIR)/pak-stage && zip -r "$(CURDIR)/$(BUILD_DIR)/release/tg5050/$(PAK_NAME).pak.zip" . -x '.*'
	@rm -rf $(BUILD_DIR)/pak-stage

package: package-tg5040 package-tg5050

# ── TrimUI .pakz export ──────────────────────────────────────

export-trimui: embedded
	@rm -rf $(BUILD_DIR)/trimui-stage
	@mkdir -p $(BUILD_DIR)/trimui-stage/Tools/tg5040/$(PAK_NAME).pak/resources/bin
	@mkdir -p $(BUILD_DIR)/trimui-stage/Tools/tg5050/$(PAK_NAME).pak/resources/bin
	@mkdir -p $(BUILD_DIR)/release/trimui
	@rm -f $(BUILD_DIR)/release/trimui/$(PAK_NAME).pakz
	cp $(BUILD_DIR)/tg5040/$(APP_NAME) $(BUILD_DIR)/trimui-stage/Tools/tg5040/$(PAK_NAME).pak/
	cp launch.sh $(BUILD_DIR)/trimui-stage/Tools/tg5040/$(PAK_NAME).pak/
	cp pak.json $(BUILD_DIR)/trimui-stage/Tools/tg5040/$(PAK_NAME).pak/
	cp LICENSE $(BUILD_DIR)/trimui-stage/Tools/tg5040/$(PAK_NAME).pak/ 2>/dev/null || true
	cp $(BUILD_DIR)/tg5040/bin/* $(BUILD_DIR)/trimui-stage/Tools/tg5040/$(PAK_NAME).pak/resources/bin/
	cp $(BUILD_DIR)/tg5050/$(APP_NAME) $(BUILD_DIR)/trimui-stage/Tools/tg5050/$(PAK_NAME).pak/
	cp launch.sh $(BUILD_DIR)/trimui-stage/Tools/tg5050/$(PAK_NAME).pak/
	cp pak.json $(BUILD_DIR)/trimui-stage/Tools/tg5050/$(PAK_NAME).pak/
	cp LICENSE $(BUILD_DIR)/trimui-stage/Tools/tg5050/$(PAK_NAME).pak/ 2>/dev/null || true
	cp $(BUILD_DIR)/tg5050/bin/* $(BUILD_DIR)/trimui-stage/Tools/tg5050/$(PAK_NAME).pak/resources/bin/
	cd $(BUILD_DIR)/trimui-stage && zip -9 -r "$(CURDIR)/$(BUILD_DIR)/release/trimui/$(PAK_NAME).pakz" . -x '.*'
	@rm -rf $(BUILD_DIR)/trimui-stage

# ── Cleanup ───────────────────────────────────────────────────

clean:
	rm -rf $(BUILD_DIR)

clean-all: clean
	rm -rf $(CACHE_DIR)

# ── Help ──────────────────────────────────────────────────────

help:
	@echo "Targets:"
	@echo "  all           Auto-detect platform and build"
	@echo "  mac           Build for macOS (native)"
	@echo "  tg5040        Build for TG5040 (Docker ARM64)"
	@echo "  tg5050        Build for TG5050 (Docker ARM64)"
	@echo "  embedded      Build all embedded platforms"
	@echo "  deps          Update Go dependencies + apply patches"
	@echo "  patch-vendor  Apply vendor patches (TG5050 power button)"
	@echo "  package       Package both platforms (.pak.zip)"
	@echo "  export-trimui Create .pakz for TrimUI Tools"
	@echo "  build-sdl2-gfx    Cross-compile static SDL2_gfx (cached in .cache/sdl2-gfx/)"
	@echo "  clean-sdl2-gfx    Remove cached SDL2_gfx (force rebuild)"
	@echo "  build-git-static  Build static git binary (cached in .cache/git-static/)"
	@echo "  clean-git-static  Remove cached static git binary (force rebuild)"
	@echo "  build-go-sdk      Download Go SDK for ARM64 (cached in .cache/go-sdk/)"
	@echo "  clean-go-sdk      Remove cached Go SDK (force re-download)"
	@echo "  clean             Remove build artifacts"
	@echo "  clean-all         Remove build + cache"

.PHONY: all mac tg5040 tg5050 embedded build-sdl2-gfx clean-sdl2-gfx build-git-static clean-git-static build-go-sdk clean-go-sdk deps patch-vendor package package-tg5040 package-tg5050 export-trimui clean clean-all help check-credentials debug-levels
