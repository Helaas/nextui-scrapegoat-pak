#!/bin/sh
PAK_DIR="$(dirname "$0")"
cd "$PAK_DIR" || exit 1

export PATH=$PAK_DIR/resources/bin:$PATH
export GIT_EXEC_PATH=$PAK_DIR/resources/bin

if [ -z "$XDG_RUNTIME_DIR" ]; then
	export XDG_RUNTIME_DIR=/tmp/runtime-root
	mkdir -p "$XDG_RUNTIME_DIR"
fi

./scrapegoat
