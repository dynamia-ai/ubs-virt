#!/bin/bash
# Copyright (c) Huawei Technologies Co., Ltd. 2025-2025. All rights reserved.
#
# enpu-manager-standalone is licensed under Mulan PSL v2.
# You can use this software according to the terms and conditions of the Mulan PSL v2.
# You may obtain a copy of Mulan PSL v2 at:
#          http://license.coscl.org.cn/MulanPSL2
# THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND,
# EITHER EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT,
# MERCHANTABILITY OR FIT FOR A PARTICULAR PURPOSE.
# See the Mulan PSL v2 for more details.

set -e

SCRIPT_DIR=$(cd "$(dirname "$0")" && pwd)
CORE_DIR="${SCRIPT_DIR}/../core"
CORE_LIB="${CORE_DIR}/build/output/libenpu_manager.so"
OUTPUT_DIR="${SCRIPT_DIR}/build/output"

if [ -z "$ASCEND_HOME_PATH" ]; then
    echo "[ERROR] ASCEND_HOME_PATH is not set!"
    exit 1
fi

CC=${CC:-gcc}
CFLAGS=(-Wall -O2 -fPIC)
CANN_PATH=${ASCEND_HOME_PATH}
DRIVER_PATH=${ENPU_ASCEND_DRIVER_PATH:-/usr/local/Ascend}

INCLUDES=(-I"${CORE_DIR}/../../common/include" -I"${CORE_DIR}/include" -I"${SCRIPT_DIR}/api" -I"${CANN_PATH}/include" -I"${CANN_PATH}/include/experiment")
LDFLAGS=(-L"${CORE_DIR}/build/output"
    -L"${CANN_PATH}/lib64"
    -L"${DRIVER_PATH}/driver/lib64"
    -L"${DRIVER_PATH}/driver/lib64/driver"
    '-Wl,-rpath,$ORIGIN/../../../core/build/output'
    -lenpu_manager -lascendcl -ldcmi -lc_sec -lcjson -lpthread)

if [[ ! -f "${CORE_LIB}" ]]; then
    echo "[ERROR] Core library not found: ${CORE_LIB}"
    echo "[INFO] Build core first: cd ../core && bash make_build.sh"
    exit 1
fi

mkdir -p "${OUTPUT_DIR}"
echo "[INFO] Using compiler: ${CC}"
echo "[INFO] Core library: ${CORE_LIB}"

echo "[INFO] Building enpu-manager..."
"${CC}" "${CFLAGS[@]}" "${INCLUDES[@]}" \
    "${SCRIPT_DIR}/api/rest_api.c" \
    "${SCRIPT_DIR}/cmd/enpu_manager_main.c" \
    -o "${OUTPUT_DIR}/enpu-manager" \
    "${LDFLAGS[@]}"

if [[ $? -ne 0 ]]; then
    echo "[ERROR] Failed to build enpu-manager"
    exit 1
fi
echo "[OK] enpu-manager -> ${OUTPUT_DIR}/enpu-manager"

echo "[INFO] Building enpu-cli..."
"${CC}" "${CFLAGS[@]}" -I"${CORE_DIR}/../../common/include" -I"${SCRIPT_DIR}/cmd" -I"${CANN_PATH}/include" \
    "${SCRIPT_DIR}/cmd/cli.c" \
    -L"${CANN_PATH}/lib64" -lc_sec -luuid \
    -o "${OUTPUT_DIR}/enpu-cli"

if [[ $? -ne 0 ]]; then
    echo "[ERROR] Failed to build enpu-cli"
    exit 1
fi
echo "[OK] enpu-cli -> ${OUTPUT_DIR}/enpu-cli"

echo "[INFO] Build complete"
ls -la "${OUTPUT_DIR}/"
