#!/bin/sh
set -eu

APP_BIN="scrapegoat"
PAK_DIR=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
PAK_NAME=$(basename "$PAK_DIR")
PAK_NAME=${PAK_NAME%.pak}

cd "$PAK_DIR"

# Bundled git binary
export PATH="$PAK_DIR/resources/bin:$PATH"
export GIT_EXEC_PATH="$PAK_DIR/resources/bin"

if [ -z "${XDG_RUNTIME_DIR:-}" ]; then
    export XDG_RUNTIME_DIR=/tmp/runtime-root
    mkdir -p "$XDG_RUNTIME_DIR"
fi

# Logging
if [ -n "${SHARED_USERDATA_PATH:-}" ]; then
    SHARED_USERDATA_ROOT="$SHARED_USERDATA_PATH"
elif [ -d "/mnt/SDCARD/.userdata/shared" ] || [ -d "/mnt/SDCARD" ]; then
    SHARED_USERDATA_ROOT="/mnt/SDCARD/.userdata/shared"
else
    SHARED_USERDATA_ROOT="${HOME:-/tmp}/.userdata/shared"
fi
LOG_ROOT=${LOGS_PATH:-"$SHARED_USERDATA_ROOT/logs"}
mkdir -p "$LOG_ROOT"
LOG_FILE="$LOG_ROOT/$APP_BIN.txt"
: >"$LOG_FILE"

exec >>"$LOG_FILE"
exec 2>&1

echo "=== Launching $PAK_NAME ($APP_BIN) at $(date) ==="
echo "platform=${PLATFORM:-unknown} device=${DEVICE:-unknown}"
echo "args: $*"

exec "./$APP_BIN" "$@"
