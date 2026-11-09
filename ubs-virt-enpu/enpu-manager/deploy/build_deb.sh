#!/bin/bash
# Copyright (c) Huawei Technologies Co., Ltd. 2026. All rights reserved.
#
# enpu-manager is licensed under Mulan PSL v2.
# You can use this software according to the terms and conditions of the Mulan PSL v2.
# You may obtain a copy of Mulan PSL v2 at:
#          http://license.coscl.org.cn/MulanPSL2
# THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND,
# EITHER EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT,
# MERCHANTABILITY OR FIT FOR A PARTICULAR PURPOSE.
# See the Mulan PSL v2 for more details.
#
# enpu-manager Debian Package Builder
# Usage:
#   ./build_deb.sh

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
PROJECT_ROOT="$(cd "${SCRIPT_DIR}/.." && pwd)"
SOURCE_ROOT="$(cd "${PROJECT_ROOT}/.." && pwd)"
PROJECT_NAME="enpu-manager"

# === Configuration ===
DEB_BUILD_DIR="${PROJECT_ROOT}/debbuild"
BUILD_NUMBER="${BUILD_NUMBER:-1}"

# Determine version from debian/changelog (canonical for Debian), fallback to VERSION file
CHANGELOG_FILE="${SCRIPT_DIR}/debian/changelog"
VERSION_FILE="${PROJECT_ROOT}/VERSION"
if [ -f "${CHANGELOG_FILE}" ] && command -v dpkg-parsechangelog &>/dev/null; then
    DEB_VERSION=$(dpkg-parsechangelog -S Version -l "${CHANGELOG_FILE}")
    VERSION="${DEB_VERSION%-*}"
elif [ -f "${VERSION_FILE}" ]; then
    VERSION=$(cat "${VERSION_FILE}")
else
    echo "[ERROR] Cannot determine version: neither debian/changelog nor VERSION file found!"
    exit 1
fi

echo "============================================"
echo "  enpu-manager DEB Package Builder"
echo "============================================"
echo "  Version:       ${VERSION}"
echo "  Architecture:  $(uname -m)"
echo "  Build Dir:     ${DEB_BUILD_DIR}"
echo "============================================"

echo ""
echo "[STEP 0] Checking environment..."
# CANN path: must be set for build
if [ -z "${ASCEND_HOME_PATH:-}" ]; then
    echo "[ERROR] ASCEND_HOME_PATH is not set!"
    echo "  Please set it before building, e.g.:"
    echo "  export ASCEND_HOME_PATH=/usr/local/Ascend/cann"
    exit 1
fi
ENPU_ASCEND_DRIVER_PATH="${ENPU_ASCEND_DRIVER_PATH:-/usr/local/Ascend}"
echo "[INFO] ASCEND_HOME_PATH=${ASCEND_HOME_PATH}"
echo "[INFO] ENPU_ASCEND_DRIVER_PATH=${ENPU_ASCEND_DRIVER_PATH}"

# Check required tools
for cmd in dpkg-buildpackage dh cmake gcc; do
    if ! command -v "$cmd" &>/dev/null; then
        echo "[ERROR] $cmd not found! Install build dependencies:"
        echo "  sudo apt-get install build-essential cmake debhelper libcjson-dev uuid-dev"
        exit 1
    fi
done

# Check for libcjson-dev
if ! dpkg -l libcjson-dev &>/dev/null 2>&1; then
    echo "[WARNING] libcjson-dev not found."
    echo "  Install it: sudo apt-get install libcjson-dev"
fi

if ! dpkg -l uuid-dev &>/dev/null 2>&1; then
    echo "[WARNING] uuid-dev not found."
    echo "  Install it: sudo apt-get install uuid-dev"
fi

echo ""
echo "[STEP 1] Setting up build directory..."
rm -rf "${DEB_BUILD_DIR}"
mkdir -p "${DEB_BUILD_DIR}"

# Export source tree from working tree (same approach as build_rpm.sh)
echo "[INFO] Exporting source tree..."
# Preserve the same sibling layout as the repository and the RPM source archive.
git -C "${SOURCE_ROOT}" ls-files --cached --others --exclude-standard -z -- enpu-manager common \
    ':(exclude)enpu-manager/rpmbuild/**' ':(exclude)enpu-manager/debbuild/**' \
    ':(exclude)enpu-manager/core/build/**' ':(exclude)enpu-manager/standalone/build/**' | \
    tar -cf - --transform "s|^|${PROJECT_NAME}-${VERSION}/|" -C "${SOURCE_ROOT}" --null -T - | \
    tar -xf - -C "${DEB_BUILD_DIR}"

echo ""
echo "[STEP 2] Setting up Debian packaging directory..."
# Copy debian packaging files into the source tree
mkdir -p "${DEB_BUILD_DIR}/${PROJECT_NAME}-${VERSION}/debian"
cp -r "${SCRIPT_DIR}/debian/"* "${DEB_BUILD_DIR}/${PROJECT_NAME}-${VERSION}/debian/"

# Generate service file with proper LD_LIBRARY_PATH
LD_PATH="${ASCEND_HOME_PATH}/lib64:${ENPU_ASCEND_DRIVER_PATH}/driver/lib64/driver"
sed -i "s|^\(Environment=LD_LIBRARY_PATH=\).*|\1${LD_PATH}|" \
    "${DEB_BUILD_DIR}/${PROJECT_NAME}-${VERSION}/enpu-manager/deploy/enpu-manager.service"
echo "[INFO] LD_LIBRARY_PATH set to: ${LD_PATH}"

echo ""
echo "[STEP 3] Building Debian package..."
cd "${DEB_BUILD_DIR}/${PROJECT_NAME}-${VERSION}"

export ASCEND_HOME_PATH
export ENPU_ASCEND_DRIVER_PATH

# Build the package with dpkg-buildpackage
dpkg-buildpackage \
    -d \
    --build=binary \
    --no-sign \
    -tc \
    -j"$(nproc 2>/dev/null || echo 4)"

echo ""
echo "============================================"
echo "  Build Complete!"
echo "============================================"

# Copy resulting .deb files to output directory
mkdir -p "${DEB_BUILD_DIR}/output"
cp "${DEB_BUILD_DIR}"/*.deb "${DEB_BUILD_DIR}/output/" 2>/dev/null || true

DEB_FILE=$(find "${DEB_BUILD_DIR}/output" -name "${PROJECT_NAME}*.deb" 2>/dev/null | head -1)

echo ""
echo "  Package location:"
if [ -n "${DEB_FILE}" ]; then
    echo "    ${DEB_FILE}"
    dpkg-deb --info "${DEB_FILE}" 2>/dev/null || true
fi
echo ""
echo "  Install locally:"
echo "    sudo dpkg -i ${DEB_FILE:-<output_deb>}"
echo "    sudo apt-get install -f   # Install missing dependencies"
echo "============================================"
