#!/bin/bash
# -----------------------------------------------------------------------------------------------------------
# Copyright (c) Huawei Technologies Co., Ltd. 2026-2026. All rights reserved.
# ubs-virt-enpu is licensed under Mulan PSL v2.
# You can use this software according to the terms and conditions of the Mulan PSL v2.
# You may obtain a copy of Mulan PSL v2 at:
#          http://license.coscl.org.cn/MulanPSL2
# THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND,
# EITHER EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT,
# MERCHANTABILITY OR FIT FOR A PARTICULAR PURPOSE.
# See the Mulan PSL v2 for more details.
# -----------------------------------------------------------------------------------------------------------

set -e

if ! command -v lcov > /dev/null 2>&1; then
    echo "[ERROR] Command lcov not found."
    exit 1
fi

COVERAGE_THRESHOLD=80.0

version_output=$(lcov --version 2>/dev/null | head -n1)
if [[ $version_output =~ ([0-9]+\.[0-9]+) ]]; then
    lcov_version="${BASH_REMATCH[1]}"
else
    lcov_version="0.0"
fi

if [ "$(printf '%s\n' "$lcov_version" "2.0" | sort -V | tail -n1)" = "$lcov_version" ]; then
    IGNORED_ERRORS="mismatch,unused,unused"
else
    IGNORED_ERRORS="gcov,source,graph"
fi

lcov --capture --directory . \
    --ignore-errors "$IGNORED_ERRORS" \
    --rc lcov_branch_coverage=1 \
    --output-file coverage_base.info
lcov --remove coverage_base.info \
    '/usr/*' '*/test/*' '*/build/*' '*/__build/*' \
    '*.h' '*.hpp' '*.hh' '*.hxx' \
    --ignore-errors "$IGNORED_ERRORS" \
    --rc lcov_branch_coverage=1 \
    --output-file coverage.info

if command -v genhtml > /dev/null 2>&1; then
    genhtml coverage.info --show-details --legend \
        --rc lcov_branch_coverage=1 \
        --output-directory coverage_report
fi

LINE_COVERAGE=$(lcov --summary coverage.info \
    --ignore-errors "$IGNORED_ERRORS" \
    --rc lcov_branch_coverage=1 2>/dev/null \
    | grep "lines" | sed -E "s#.*: *([0-9]+\.?[0-9]*)% .*#\1#" \
    || echo "0.0")

if (( $(echo "$LINE_COVERAGE >= $COVERAGE_THRESHOLD" | bc -l) )); then
    echo "[INFO] Coverage check PASSED: $LINE_COVERAGE% >= $COVERAGE_THRESHOLD%."
else
    echo "[ERROR] Coverage check FAILED: $LINE_COVERAGE% < $COVERAGE_THRESHOLD%."
    exit 1
fi
