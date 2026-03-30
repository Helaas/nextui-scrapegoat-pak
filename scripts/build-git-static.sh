#!/bin/sh
# build-git-static.sh — Compile a minimal static git binary for ARM64 with HTTPS support.
# Runs inside an Alpine Docker container. Outputs: /out/git, /out/git-remote-https
#
# We build our own minimal OpenSSL + libcurl (HTTPS only) to keep the
# binaries as small as possible. Alpine's stock OpenSSL static lib is full-
# featured (~5-6 MB); our minimal build cuts that significantly.

set -e

GIT_VERSION="${GIT_VERSION:-2.47.2}"
CURL_VERSION="${CURL_VERSION:-8.11.1}"
OPENSSL_VERSION="${OPENSSL_VERSION:-3.3.2}"

COMMON_CFLAGS="-Os -ffunction-sections -fdata-sections"
COMMON_LDFLAGS="-Wl,--gc-sections"

echo "build-git-static: git=${GIT_VERSION} curl=${CURL_VERSION} openssl=${OPENSSL_VERSION}"

# Build tools + zlib (OpenSSL is built from source below).
apk add --no-cache build-base perl wget ca-certificates \
    zlib-dev zlib-static linux-headers

# ── Minimal static OpenSSL ────────────────────────────────────────────────────
# Only what TLS 1.2/1.3 to GitHub needs: AES-GCM, SHA-256, ECDHE, X25519.
# Everything else (legacy ciphers, DTLS, CMS, OCSP, engines, etc.) is stripped.

echo "build-git-static: building openssl ${OPENSSL_VERSION}..."
wget -q "https://www.openssl.org/source/openssl-${OPENSSL_VERSION}.tar.gz" \
     -O /tmp/openssl.tar.gz
tar -C /tmp -xf /tmp/openssl.tar.gz
cd "/tmp/openssl-${OPENSSL_VERSION}"

CFLAGS="$COMMON_CFLAGS" \
perl ./Configure linux-aarch64 \
    --prefix=/opt/openssl \
    --openssldir=/opt/openssl/ssl \
    --libdir=lib \
    no-shared no-tests no-async \
    no-apps \
    no-afalgeng no-capieng no-padlockeng no-devcryptoeng \
    no-cmp no-cms no-ct no-ocsp no-ts \
    no-srp no-gost no-legacy \
    no-comp no-nextprotoneg no-psk \
    no-dtls no-dtls1 no-dtls1_2 \
    no-engine \
    no-idea no-md2 no-mdc2 no-rc5 no-whirlpool \
    no-seed no-sm2 no-sm3 no-sm4 \
    no-camellia no-cast no-bf \
    no-scrypt no-siphash

make -j"$(nproc)"
make install_sw
echo "build-git-static: openssl done"

# ── Minimal static libcurl (HTTPS only) ──────────────────────────────────────
# Build without nghttp2, brotli, libpsl, libidn2, librtmp, libssh2, kerberos,
# LDAP, RTMP and all non-HTTP protocols. Uses our minimal OpenSSL + zlib.

echo "build-git-static: building libcurl ${CURL_VERSION}..."
wget -q "https://curl.se/download/curl-${CURL_VERSION}.tar.gz" -O /tmp/curl.tar.gz
tar -C /tmp -xf /tmp/curl.tar.gz
cd "/tmp/curl-${CURL_VERSION}"

CFLAGS="$COMMON_CFLAGS" \
LDFLAGS="$COMMON_LDFLAGS" \
./configure \
    --disable-shared --enable-static \
    --with-openssl=/opt/openssl \
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
    CFLAGS="-I/opt/curl/include -I/opt/openssl/include $COMMON_CFLAGS" \
    LDFLAGS="-static -L/opt/curl/lib -L/opt/openssl/lib $COMMON_LDFLAGS" \
    NO_GETTEXT=1 \
    NO_ICONV=1 \
    NO_PERL=1 \
    NO_PYTHON=1 \
    NO_TCLTK=1 \
    NO_EXPAT=1 \
    NO_REGEX=NeedsStartEnd

echo "build-git-static: git done"

# ── Strip & copy ─────────────────────────────────────────────────────────────

strip git
cp git /out/git

if strip git-remote-https 2>/dev/null; then
    cp git-remote-https /out/git-remote-https
    echo "build-git-static: copied git-remote-https"
else
    echo "build-git-static: WARNING — git-remote-https not found; HTTPS clones will fail"
fi

echo "build-git-static: git $(ls -lh /out/git | awk '{print $5}')"
if [ -f /out/git-remote-https ]; then
    echo "build-git-static: git-remote-https $(ls -lh /out/git-remote-https | awk '{print $5}')"
fi
echo "build-git-static: done"
ls -la /out/
