#!/usr/bin/env bash
set -euo pipefail

# build_third_party.sh — Build static OpenSSL + curl for ARM64 cross-compilation.
# Project-owned copy adapted from Apostrophe's scripts/build_third_party.sh.
# Changes from upstream:
#   - REPO_ROOT resolves to this project's root (one level up from scripts/)
#   - Atomic downloads with .tmp files + automatic retry on checksum mismatch

usage() {
    cat <<'EOF'
Usage:
  build_third_party.sh ensure-curl <platform>
  build_third_party.sh stage-runtime-libs <platform> <binary> <dest_dir>

Platforms:
  tg5040 | tg5050 | my355
EOF
}

if [[ $# -lt 2 ]]; then
    usage
    exit 1
fi

ACTION="$1"
PLATFORM="$2"
case "$PLATFORM" in
    tg5040|tg5050|my355) ;;
    *)
        echo "Unsupported platform: $PLATFORM" >&2
        exit 1
        ;;
esac

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO_ROOT="$(cd "$SCRIPT_DIR/.." && pwd)"
BUILD_ROOT="$REPO_ROOT/build/third_party"
SOURCES_DIR="$BUILD_ROOT/sources"
PLATFORM_ROOT="$BUILD_ROOT/$PLATFORM"
SYSROOT="/opt/aarch64-nextui-linux-gnu/aarch64-nextui-linux-gnu/libc/usr"
TRIPLET="aarch64-nextui-linux-gnu"
JOBS="${JOBS:-$(getconf _NPROCESSORS_ONLN 2>/dev/null || echo 4)}"

CURL_VERSION="8.7.1"
CURL_ARCHIVE="curl-${CURL_VERSION}.tar.xz"
CURL_URL="https://curl.se/download/${CURL_ARCHIVE}"
CURL_SHA256="6fea2aac6a4610fbd0400afb0bcddbe7258a64c63f1f68e5855ebc0c659710cd"

CACERT_URL="https://curl.se/ca/cacert-2025-12-02.pem"
CACERT_FILE="cacert-2025-12-02.pem"
CACERT_SHA256="f1407d974c5ed87d544bd931a278232e13925177e239fca370619aba63c757b4"

OPENSSL_VERSION="3.3.2"
OPENSSL_ARCHIVE="openssl-${OPENSSL_VERSION}.tar.gz"
OPENSSL_URL="https://www.openssl.org/source/${OPENSSL_ARCHIVE}"
OPENSSL_SHA256="2e8a40b01979afe8be0bbfb3de5dc1c6709fedb46d6c89c10da114ab5fc3d281"

mkdir -p "$SOURCES_DIR" "$PLATFORM_ROOT"

download_and_verify() {
    local url="$1"
    local archive="$2"
    local expected_sha="$3"
    local path="$SOURCES_DIR/$archive"
    local tmp_path="$path.tmp"
    local actual_sha=""

    if [[ ! -f "$path" ]]; then
        echo "[third_party] downloading $archive"
        rm -f "$tmp_path"
        wget -O "$tmp_path" "$url"
        mv "$tmp_path" "$path"
    fi

    actual_sha="$(sha256sum "$path" | awk '{print $1}')"
    if [[ "$actual_sha" != "$expected_sha" ]]; then
        echo "[third_party] checksum mismatch for cached $archive, re-downloading" >&2
        echo "  expected: $expected_sha" >&2
        echo "  actual:   $actual_sha" >&2
        rm -f "$path" "$tmp_path"
        wget -O "$tmp_path" "$url"
        mv "$tmp_path" "$path"
        actual_sha="$(sha256sum "$path" | awk '{print $1}')"
        if [[ "$actual_sha" != "$expected_sha" ]]; then
            echo "[third_party] checksum mismatch for $archive" >&2
            echo "  expected: $expected_sha" >&2
            echo "  actual:   $actual_sha" >&2
            exit 1
        fi
    fi
}

extract_source() {
    local archive="$1"
    local dest="$2"
    rm -rf "$dest"
    mkdir -p "$dest"
    tar -xf "$SOURCES_DIR/$archive" --strip-components=1 -C "$dest"
}

ensure_openssl() {
    local prefix="$PLATFORM_ROOT/openssl/install"
    local stamp="$prefix/.stamp-openssl-${OPENSSL_VERSION}-static2"
    local src_dir="$PLATFORM_ROOT/openssl/src"

    if [[ -f "$stamp" ]]; then
        return
    fi

    download_and_verify "$OPENSSL_URL" "$OPENSSL_ARCHIVE" "$OPENSSL_SHA256"
    extract_source "$OPENSSL_ARCHIVE" "$src_dir"

    mkdir -p "$prefix"
    pushd "$src_dir" >/dev/null
    echo "[third_party] building openssl ${OPENSSL_VERSION} for $PLATFORM"
    CC="gcc" AR="ar" RANLIB="ranlib" CROSS_COMPILE="${TRIPLET}-" \
    CFLAGS="-ffunction-sections -fdata-sections -O2" \
    perl ./Configure linux-aarch64 \
        --prefix="$prefix" \
        --openssldir="$prefix/ssl" \
        --libdir=lib \
        no-shared no-tests no-async \
        no-apps \
        no-afalgeng no-capieng no-padlockeng no-devcryptoeng \
        no-cmp no-cms no-ct no-ocsp no-ts \
        no-srp no-gost no-legacy \
        no-comp no-nextprotoneg no-psk
    make -j"$JOBS"
    make install_sw
    popd >/dev/null

    touch "$stamp"
}

ensure_curl() {
    local prefix="$PLATFORM_ROOT/curl/install"
    local stamp="$prefix/.stamp-curl-${CURL_VERSION}-static2"
    local src_dir="$PLATFORM_ROOT/curl/src"
    local openssl_prefix="$PLATFORM_ROOT/openssl/install"
    local cppflags="-I${openssl_prefix}/include -I${SYSROOT}/include"
    local ldflags="-L${openssl_prefix}/lib -L${SYSROOT}/lib -Wl,-rpath-link,${openssl_prefix}/lib -Wl,-rpath-link,${SYSROOT}/lib"

    if [[ -f "$stamp" ]]; then
        return
    fi

    ensure_openssl

    download_and_verify "$CURL_URL" "$CURL_ARCHIVE" "$CURL_SHA256"
    extract_source "$CURL_ARCHIVE" "$src_dir"

    mkdir -p "$prefix"
    pushd "$src_dir" >/dev/null
    echo "[third_party] building curl ${CURL_VERSION} for $PLATFORM"
    CC="${TRIPLET}-gcc" \
    AR="${TRIPLET}-ar" \
    RANLIB="${TRIPLET}-ranlib" \
    STRIP="${TRIPLET}-strip" \
    LD="${TRIPLET}-ld" \
    PKG_CONFIG=false \
    CFLAGS="-ffunction-sections -fdata-sections -O2" \
    CPPFLAGS="$cppflags" \
    LDFLAGS="$ldflags" \
    ./configure \
        --build=x86_64-pc-linux-gnu \
        --host="$TRIPLET" \
        --prefix="$prefix" \
        --libdir="$prefix/lib" \
        --with-openssl="$openssl_prefix" \
        --with-zlib="$SYSROOT" \
        --disable-shared \
        --enable-static \
        --without-libidn2 \
        --without-libpsl \
        --without-nghttp2 \
        --without-brotli \
        --without-zstd \
        --without-libssh2 \
        --without-librtmp \
        --without-ca-bundle \
        --without-ca-path \
        --disable-manual \
        --disable-dict \
        --disable-file \
        --disable-ftp \
        --disable-gopher \
        --disable-imap \
        --disable-ldap \
        --disable-mqtt \
        --disable-pop3 \
        --disable-rtsp \
        --disable-smb \
        --disable-smtp \
        --disable-telnet \
        --disable-tftp \
        --disable-unix-sockets
    make -j"$JOBS"
    make install
    popd >/dev/null

    touch "$stamp"
}

stage_runtime_libs() {
    local binary="$3"
    local dest_dir="$4"

    if [[ ! -f "$binary" ]]; then
        echo "Binary not found: $binary" >&2
        exit 1
    fi

    mkdir -p "$dest_dir"

    # Static build — only the CA certificate bundle is needed at runtime
    download_and_verify "$CACERT_URL" "$CACERT_FILE" "$CACERT_SHA256"
    cp "$SOURCES_DIR/$CACERT_FILE" "$dest_dir/cacert.pem"
}

case "$ACTION" in
    ensure-curl)
        ensure_curl
        ;;
    stage-runtime-libs)
        if [[ $# -ne 4 ]]; then
            usage
            exit 1
        fi
        ensure_curl
        stage_runtime_libs "$@"
        ;;
    *)
        usage
        exit 1
        ;;
esac
