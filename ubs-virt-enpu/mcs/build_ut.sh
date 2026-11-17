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

if [ -z "$ASCEND_HOME_PATH" ]; then
    echo "[ERROR] ASCEND_HOME_PATH is not set!"
    exit 1
fi

mkdir -p __build
cd __build

cmake .. -DBUILD_TESTS=ON -DENABLE_COVERAGE=ON -DCMAKE_BUILD_TYPE=Debug
make mcs_test mcs_memory_test -j "$(nproc)"

./test/mcs_test --gtest_output=xml:test_detail.xml
./test/mcs_memory_test --gtest_output=xml:memory_test_detail.xml

bash ../coverage.sh
