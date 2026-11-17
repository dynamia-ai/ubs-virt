/*
 * Copyright (c) Huawei Technologies Co., Ltd. 2026-2026. All rights reserved.
 * ubs-virt-enpu is licensed under Mulan PSL v2.
 * You can use this software according to the terms and conditions of the Mulan PSL v2.
 * You may obtain a copy of Mulan PSL v2 at:
 *          http://license.coscl.org.cn/MulanPSL2
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND,
 * EITHER EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT,
 * MERCHANTABILITY OR FIT FOR A PARTICULAR PURPOSE.
 * See the Mulan PSL v2 for more details.
 */

#include <gtest/gtest.h>
#include "mcs_device.h"

TEST(DeviceTest, CalculatesLimitedCubeAndVectorCounts)
{
    mcs_device_init(32, 16, 50);
    EXPECT_EQ(mcs_get_limited_aicube(), 16);
    EXPECT_EQ(mcs_get_limited_aivector(), 8);
}

TEST(DeviceTest, ReturnsLimitedCountsForSupportedQueries)
{
    mcs_device_init(40, 20, 25);
    int64_t value = -1;
    EXPECT_EQ(mcs_rtGetDeviceInfo_hook(0, MCS_MODULE_TYPE_AICORE, MCS_INFO_TYPE_CORE_NUM, &value), 0);
    EXPECT_EQ(value, 10);
    EXPECT_EQ(mcs_rtGetDeviceInfo_hook(0, MCS_MODULE_TYPE_AICORE, MCS_INFO_TYPE_CUBE_NUM, &value), 0);
    EXPECT_EQ(value, 10);
    EXPECT_EQ(mcs_rtGetDeviceInfo_hook(0, MCS_MODULE_TYPE_VECTOR_CORE, MCS_INFO_TYPE_CORE_NUM, &value), 0);
    EXPECT_EQ(value, 5);
}

TEST(DeviceTest, RejectsNullAndPassThroughQueries)
{
    mcs_device_init(32, 16, 50);
    int64_t value = 123;
    EXPECT_EQ(mcs_rtGetDeviceInfo_hook(0, MCS_MODULE_TYPE_AICORE, MCS_INFO_TYPE_CORE_NUM, nullptr), -1);
    EXPECT_EQ(mcs_rtGetDeviceInfo_hook(0, 99, MCS_INFO_TYPE_CORE_NUM, &value), -1);
    EXPECT_EQ(mcs_rtGetDeviceInfo_hook(0, MCS_MODULE_TYPE_VECTOR_CORE, 99, &value), -1);
    EXPECT_EQ(value, 123);
}
