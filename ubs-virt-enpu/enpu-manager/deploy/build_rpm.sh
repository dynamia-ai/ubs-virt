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
# enpu-manager RPM Builder
# Usage:
#   ./build_rpm.sh

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
PROJECT_ROOT="$(cd "${SCRIPT_DIR}/.." && pwd)"
SOURCE_ROOT="$(cd "${PROJECT_ROOT}/.." && pwd)"
PROJECT_NAME="enpu-manager"

# === Configuration ===
RPMBUILD_DIR="${PROJECT_ROOT}/rpmbuild"
SPEC_FILE="${SCRIPT_DIR}/${PROJECT_NAME}.spec"
ARCH="$(uname -m)"

# Determine project version from spec file
SPEC_VERSION=$(grep -E '^Version:' "${SPEC_FILE}" | awk '{print $2}')
SPEC_RELEASE=$(grep -E '^Release:' "${SPEC_FILE}" | awk '{print $2}' | sed 's/%{?dist}//')
VERSION="${SPEC_VERSION}"
RELEASE="${SPEC_RELEASE}"

echo "============================================"
echo "  enpu-manager RPM Builder"
echo "============================================"
echo "  Version:       ${VERSION}"
echo "  Release:       ${RELEASE}"
echo "  Architecture:  ${ARCH}"
echo "  RPM Build Dir: ${RPMBUILD_DIR}"
echo "============================================"

echo ""
echo "[STEP 0] Generating service file from environment..."
# CANN path: must be set for build
if [ -z "${ASCEND_HOME_PATH:-}" ]; then
    echo "[ERROR] ASCEND_HOME_PATH is not set!"
    echo "  Please set it before building, e.g.:"
    echo "  export ASCEND_HOME_PATH=/usr/local/Ascend/cann"
    exit 1
fi
ENPU_ASCEND_DRIVER_PATH="${ENPU_ASCEND_DRIVER_PATH:-/usr/local/Ascend}"
echo "[INFO] ENPU_ASCEND_DRIVER_PATH set to: ${ENPU_ASCEND_DRIVER_PATH}"

# Override LD_LIBRARY_PATH in service file (save/restore to avoid dirty git tree)
LD_PATH="${ASCEND_HOME_PATH}/lib64:${ENPU_ASCEND_DRIVER_PATH}/driver/lib64/driver"
SERVICE_FILE="${SCRIPT_DIR}/enpu-manager.service"
cp "${SERVICE_FILE}" "${SERVICE_FILE}.bak"
trap 'mv -f "${SERVICE_FILE}.bak" "${SERVICE_FILE}" 2>/dev/null || true' EXIT INT TERM
sed -i "s|^\(Environment=LD_LIBRARY_PATH=\).*|\1${LD_PATH}|" "${SERVICE_FILE}"
echo "[INFO] LD_LIBRARY_PATH set to: ${LD_PATH}"

echo ""
echo "[STEP 1] Setting up RPM build tree..."
mkdir -p "${RPMBUILD_DIR}"/{BUILD,BUILDROOT,RPMS,SOURCES,SPECS,SRPMS}

echo ""
echo "[STEP 2] Creating source tarball..."
TARBALL="${RPMBUILD_DIR}/SOURCES/${PROJECT_NAME}-${VERSION}.tar.gz"

# Keep enpu-manager/ and common/ as siblings so their relative build paths work.
# Include new working-tree sources, but never recursively archive build outputs.
git -C "${SOURCE_ROOT}" ls-files --cached --others --exclude-standard -z -- enpu-manager common \
    ':(exclude)enpu-manager/rpmbuild/**' ':(exclude)enpu-manager/debbuild/**' \
    ':(exclude)enpu-manager/core/build/**' ':(exclude)enpu-manager/standalone/build/**' | \
    tar -czf "${TARBALL}" --transform "s|^|${PROJECT_NAME}-${VERSION}/|" \
        -C "${SOURCE_ROOT}" --null -T -

# Restore original service file
mv -f "${SERVICE_FILE}.bak" "${SERVICE_FILE}"

echo "[OK] Source tarball created: ${TARBALL}"
ls -lh "${TARBALL}"

echo ""
echo "[STEP 3] Copying SPEC file..."
cp "${SPEC_FILE}" "${RPMBUILD_DIR}/SPECS/"

echo ""
echo "[STEP 4] Building RPM..."
RPMBUILD_OPTS=(
    -bb
    --define "_topdir ${RPMBUILD_DIR}"
    --define "_ascend_home_path ${ASCEND_HOME_PATH}"
    --define "_enpu_ascend_driver_path ${ENPU_ASCEND_DRIVER_PATH}"
    --target "${ARCH}"
    "${RPMBUILD_DIR}/SPECS/${PROJECT_NAME}.spec"
)

rpmbuild "${RPMBUILD_OPTS[@]}"

# === Output Results ===
echo ""
echo "============================================"
echo "  Build Complete!"
echo "============================================"

RPM_DIR="${RPMBUILD_DIR}/RPMS/${ARCH}"
RPM_FILE=$(find "${RPM_DIR}" -name "${PROJECT_NAME}*.rpm" 2>/dev/null | head -1)

echo ""
echo "  Install locally:"
echo "    dnf install ${RPM_FILE}"
echo "    rpm -ivh ${RPM_FILE}"
echo "============================================"
