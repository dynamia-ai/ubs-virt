#!/bin/bash
set -e
PROJECT_NAME=mcs
VERSION=1.0

CURRENT_PATH=$(cd "$(dirname "$0")"; pwd)

if [ -z "$ASCEND_HOME_PATH" ]; then
    echo "[ERROR] ASCEND_HOME_PATH is not set!"
    exit 1
fi

BUILD_PATH="$CURRENT_PATH/build"
mkdir -p "$BUILD_PATH"
cd "$BUILD_PATH"

CMAKE_CMD="cmake .."

if ! eval "$CMAKE_CMD"; then
    echo "[ERROR] make_build:cmake failed."
    exit 1
fi

if ! make -j $(nproc); then
    echo "[ERROR] make_build:make failed."
    exit 1
fi
