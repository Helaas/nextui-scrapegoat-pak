#!/bin/sh
# build-git-static.sh — Compile a minimal static git binary for ARM64 with HTTPS support.
# Runs inside Alpine Linux. Outputs: /out/git, /out/git-remote-https
#
# We build our own minimal libcurl (HTTPS only, OpenSSL) to avoid Alpine's
# libcurl pulling in Kerberos, LDAP, brotli, etc.

set -e

GIT_VERSION="${GIT_VERSION:-2.47.2}"
CURL_VERSION="${CURL_VERSION:-8.11.1}"

echo "build-git-static: git=${GIT_VERSION} curl=${CURL_VERSION}"

# Build tools + static OpenSSL and zlib (provided by Alpine packages).
apk add --no-cache build-base perl wget ca-certificates \
    zlib-dev zlib-static openssl-dev openssl-libs-static

# ── Minimal static libcurl (HTTPS only) ──────────────────────────────────────
# Build without nghttp2, brotli, libpsl, libidn2, librtmp, libssh2, kerberos,
# LDAP, RTMP and all non-HTTP protocols. Only OpenSSL + zlib are needed.

echo "build-git-static: building libcurl ${CURL_VERSION}..."
wget -q "https://curl.se/download/curl-${CURL_VERSION}.tar.gz" -O /tmp/curl.tar.gz
tar -C /tmp -xf /tmp/curl.tar.gz
cd "/tmp/curl-${CURL_VERSION}"

./configure \
    --disable-shared --enable-static \
    --with-openssl \
    --without-nghttp2 \
    --without-brotli \
    --without-zstd \
    --without-libpsl \
    --without-libidn2 \
    --without-librtmp \
    --without-libssh2 \
    --without-gssapi \
    --disable-dict \
    --disable-file \
    --disable-ftp \
    --disable-gopher \
    --disable-imap \
    --disable-ldap \
    --disable-ldaps \
    --disable-mqtt \
    --disable-pop3 \
    --disable-rtsp \
    --disable-smtp \
    --disable-telnet \
    --disable-tftp \
    --disable-unix-sockets \
    --prefix=/opt/curl

make -j"$(nproc)" install
echo "build-git-static: libcurl done"

# ── Git ───────────────────────────────────────────────────────────────────────

echo "build-git-static: building git ${GIT_VERSION}..."
wget -q "https://github.com/git/git/archive/refs/tags/v${GIT_VERSION}.tar.gz" \
     -O /tmp/git.tar.gz
tar -C /tmp -xf /tmp/git.tar.gz
cd "/tmp/git-${GIT_VERSION}"

# Git's Makefile works without running ./configure; we pass all settings via
# make variables. CURL_CONFIG points to our minimal install so git uses it
# for --libs / --cflags detection instead of any system libcurl.
make -j"$(nproc)" \
    CURL_CONFIG=/opt/curl/bin/curl-config \
    CFLAGS="-I/opt/curl/include -Os" \
    LDFLAGS="-static -L/opt/curl/lib" \
    NO_GETTEXT=1 \
    NO_ICONV=1 \
    NO_PERL=1 \
    NO_PYTHON=1 \
    NO_TCLTK=1 \
    NO_EXPAT=1 \
    NO_REGEX=NeedsStartEnd

echo "build-git-static: git done"

cp git /out/git

if cp git-remote-https /out/git-remote-https 2>/dev/null; then
    echo "build-git-static: copied git-remote-https"
else
    echo "build-git-static: WARNING — git-remote-https not found; HTTPS clones will fail"
fi

echo "build-git-static: git size $(ls -lh /out/git | awk '{print $5}')"
echo "build-git-static: done"
ls -la /out/
