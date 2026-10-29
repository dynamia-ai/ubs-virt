#!/bin/bash
##########################################################################################################
# Copyright (c) Huawei Technologies Co., Ltd. 2026. All rights reserved.
#
# ub-device-manager is licensed under the Mulan PSL v2.
##########################################################################################################
# Build the ub-device-manager RPM package.
# Dependencies: rpm-build tar
# Usage: run "bash build_rpm.sh" on an openEuler 24.03 LTS SP4 (aarch64) server.

set -e

NAME=ub-device-manager
VERSION=1.0.0
PYTHON_VERSION="${PYTHON_VERSION:-3.11}"

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
TOPDIR="${RPM_TOPDIR:-${SCRIPT_DIR}/build/rpmbuild}"
OUTPUT_DIR="${SCRIPT_DIR}/output"
mkdir -p "${TOPDIR}"/{BUILD,RPMS,SOURCES,SPECS,SRPMS}
mkdir -p "${OUTPUT_DIR}"
rm -f "${OUTPUT_DIR}/${NAME}-"*.rpm

TARBALL="${TOPDIR}/SOURCES/${NAME}-${VERSION}.tar.gz"
tar --exclude='__pycache__' --exclude='*.pyc' --exclude='.git' \
    -czf "${TARBALL}" -C "${SCRIPT_DIR}" ub_device_manager requirements.txt
echo "Source tarball: ${TARBALL}"

cp "${SCRIPT_DIR}/ub_device_manager.spec" "${TOPDIR}/SPECS/"
rpmbuild -ba "${TOPDIR}/SPECS/ub_device_manager.spec" \
    --define "_topdir ${TOPDIR}" \
    --define "python_v ${PYTHON_VERSION}"

mapfile -t RPM_FILES < <(find "${TOPDIR}/RPMS" -type f -name "${NAME}-*.rpm")
if [ "${#RPM_FILES[@]}" -eq 0 ]; then
    echo "No binary RPM was generated."
    exit 1
fi

cp -f "${RPM_FILES[@]}" "${OUTPUT_DIR}/"

echo ""
echo "==== Build finished, RPM located at: ${OUTPUT_DIR} ===="
find "${OUTPUT_DIR}" -maxdepth 1 -type f -name "${NAME}-*.rpm" -print
