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
#include <cstdint>

#include "ascend_compat.h"
#include "mcs_config.h"
#include "mem_limiter.h"

namespace {
constexpr size_t kMb = 1024UL * 1024UL;

class MemLimiterTest : public testing::Test {
protected:
    void SetUp() override
    {
        g_mcs_config.memory_quota = 1;
        size_t used = 0;
        ASSERT_EQ(get_mem_used(&used), 0);
        on_mem_free(used);
    }
};
} // namespace

TEST_F(MemLimiterTest, ConvertsQuotaFromMegabytesToBytes)
{
    EXPECT_EQ(get_mem_limit_quota(), kMb);
    EXPECT_EQ(get_mem_used(nullptr), -1);
}

TEST_F(MemLimiterTest, TracksLiveUsageAndSaturatesOnExtraFree)
{
    on_mem_alloc(100);
    on_mem_alloc(50);
    size_t used = 0;
    EXPECT_EQ(get_mem_used(&used), 0);
    EXPECT_EQ(used, 150U);

    on_mem_free(50);
    EXPECT_EQ(get_mem_used(&used), 0);
    EXPECT_EQ(used, 100U);

    on_mem_free(1000);
    EXPECT_EQ(get_mem_used(&used), 0);
    EXPECT_EQ(used, 0U);
}

TEST_F(MemLimiterTest, ChecksQuotaBoundaryAndOverflow)
{
    EXPECT_TRUE(memory_check(kMb));
    on_mem_alloc(kMb);
    EXPECT_TRUE(memory_check(0));
    EXPECT_FALSE(memory_check(1));
    EXPECT_EQ(guard_memory(1), ACL_ERROR_STORAGE_OVER_LIMIT);

    SetUp();
    on_mem_alloc(SIZE_MAX / 2);
    EXPECT_FALSE(memory_check(SIZE_MAX));
    EXPECT_EQ(guard_memory(SIZE_MAX), ACL_ERROR_STORAGE_OVER_LIMIT);
}

TEST_F(MemLimiterTest, GuardMemoryReturnsSuccessAtAndBelowQuota)
{
    EXPECT_EQ(guard_memory(0), 0);
    EXPECT_EQ(guard_memory(kMb), 0);
    on_mem_alloc(1);
    EXPECT_EQ(guard_memory(kMb - 1), 0);
    EXPECT_EQ(guard_memory(kMb), ACL_ERROR_STORAGE_OVER_LIMIT);
}
