#!/bin/sh
# Builds libmgba with Lua scripting, for the headless link test harness.
# Usage: build_mgba.sh DIR [VERSION]
set -e
dir=$1
version=${2:-0.10.5}
git clone --depth 1 --branch "$version" https://github.com/mgba-emu/mgba "$dir"
cmake -S "$dir" -B "$dir/build" -DCMAKE_BUILD_TYPE=Release \
	-DBUILD_QT=OFF -DBUILD_SDL=OFF -DBUILD_GL=OFF -DBUILD_GLES2=OFF -DBUILD_GLES3=OFF \
	-DUSE_FFMPEG=OFF -DUSE_EPOXY=OFF -DUSE_DISCORD_RPC=OFF -DUSE_SQLITE3=OFF -DUSE_ELF=OFF \
	-DUSE_LIBZIP=OFF -DUSE_MINIZIP=OFF -DENABLE_SCRIPTING=ON -DUSE_LUA=5.4 \
	-DBUILD_STATIC=ON -DBUILD_SHARED=OFF
cmake --build "$dir/build" -j"$(nproc)"
