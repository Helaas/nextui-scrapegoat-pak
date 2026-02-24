#!/bin/sh
# bundle-git.sh — Copy pre-built static git binary into pak resources.
# Run inside Docker after the Go build.
#
# The static git binary is built separately by scripts/build-git-static.sh
# (Alpine + musl) and cached in .cache/git-static/. No shared libs needed.
#
# Usage: bundle-git.sh <build_dir>
#   build_dir: e.g. build/tg5040 — must already exist.

set -e

BUILD_DIR="${1:?Usage: bundle-git.sh <build_dir>}"
GIT_CACHE="/build/.cache/git-static"

mkdir -p "$BUILD_DIR/bin"

cp "$GIT_CACHE/git" "$BUILD_DIR/bin/git"
echo "bundle-git: git ($(ls -lh "$BUILD_DIR/bin/git" | awk '{print $5}'))"

if [ -f "$GIT_CACHE/git-remote-https" ]; then
    cp "$GIT_CACHE/git-remote-https" "$BUILD_DIR/bin/git-remote-https"
    echo "bundle-git: git-remote-https ($(ls -lh "$BUILD_DIR/bin/git-remote-https" | awk '{print $5}'))"
else
    echo "bundle-git: WARNING — git-remote-https not found; HTTPS clones will fail"
fi

echo "bundle-git: done"
ls -la "$BUILD_DIR/bin/"
