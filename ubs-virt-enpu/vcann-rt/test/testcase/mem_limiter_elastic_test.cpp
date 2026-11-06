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
#include <sys/file.h>
#include <mockcpp/mockcpp.hpp>
#include "mem_limiter.h"
#include "npu_manager.h"
#include "securec.h"
#include "shm_manager.h"

static size_t g_stub_mem_used = 0;
static size_t g_stub_mem_request_quota = 0;
static size_t g_stub_mem_limit_quota = 0;
static uint64_t g_stub_dynamic_free = 0;
static int g_stub_get_mem_used_ret = 0;

static int stub_get_mem_used(size_t *used)
{
    *used = g_stub_mem_used;
    return g_stub_get_mem_used_ret;
}

static size_t stub_get_mem_request_quota(void)
{
    return g_stub_mem_request_quota;
}

static size_t stub_get_mem_limit_quota(void)
{
    return g_stub_mem_limit_quota;
}

static uint64_t stub_get_mem_dynamic_free(void)
{
    return g_stub_dynamic_free;
}

static size_t stub_get_hbm_request_free(void)
{
    return g_stub_dynamic_free;
}

class MemLimiterElasticTest : public testing::Test {
protected:
    static void SetUpTestCase()
    {
        std::cout << "Mem Limiter Elastic test start" << std::endl;
    }

    static void TearDownTestCase()
    {
        std::cout << "Mem Limiter Elastic test end" << std::endl;
    }

    void SetUp()
    {
        g_stub_mem_used = 0;
        g_stub_mem_request_quota = 1024 * 1024 * 1024;      // 1GB request
        g_stub_mem_limit_quota = 2ULL * 1024 * 1024 * 1024; // 2GB limit
        g_stub_dynamic_free = 1024 * 1024 * 1024;           // 1GB dynamic free
        g_stub_get_mem_used_ret = 0;

        MOCKER(get_mem_used).stubs().will(invoke(stub_get_mem_used));
        MOCKER(get_mem_request_quota).stubs().will(invoke(stub_get_mem_request_quota));
        MOCKER(get_mem_limit_quota).stubs().will(invoke(stub_get_mem_limit_quota));
        MOCKER(get_mem_dynamic_free).stubs().will(invoke(stub_get_mem_dynamic_free));
        MOCKER(get_hbm_request_free).stubs().will(invoke(stub_get_hbm_request_free));
        MOCKER(check_and_swap_out).stubs().will(returnValue(ENPU_SUCCESS));
    }

    void TearDown()
    {
        GlobalMockObject::verify();
        GlobalMockObject::reset();
    }
};

TEST_F(MemLimiterElasticTest, within_request_quota_returns_true)
{
    g_stub_mem_used = 100 * 1024 * 1024;           // 100MB used
    g_stub_mem_request_quota = 1024 * 1024 * 1024; // 1GB request

    size_t requested = 500 * 1024 * 1024; // 500MB request
    bool result = memory_check_elastic(requested);

    EXPECT_TRUE(result);
}

TEST_F(MemLimiterElasticTest, exactly_at_request_quota_returns_true)
{
    g_stub_mem_used = 500 * 1024 * 1024;           // 500MB used
    g_stub_mem_request_quota = 1024 * 1024 * 1024; // 1GB request

    size_t requested = 524 * 1024 * 1024; // 524MB request (500 + 524 = 1024MB = 1GB)
    bool result = memory_check_elastic(requested);

    EXPECT_TRUE(result);
}

TEST_F(MemLimiterElasticTest, borrow_with_sufficient_dynamic_free_returns_true)
{
    g_stub_mem_used = 1024 * 1024 * 1024;               // 1GB used (at request limit)
    g_stub_mem_request_quota = 1024 * 1024 * 1024;      // 1GB request
    g_stub_mem_limit_quota = 2ULL * 1024 * 1024 * 1024; // 2GB limit
    g_stub_dynamic_free = 512 * 1024 * 1024;            // 512MB dynamic free

    size_t requested = 256 * 1024 * 1024; // 256MB (need to borrow 256MB, dynamic free has 512MB)
    bool result = memory_check_elastic(requested);

    EXPECT_TRUE(result);
}

TEST_F(MemLimiterElasticTest, borrow_with_insufficient_dynamic_free_returns_false)
{
    g_stub_mem_used = 1024 * 1024 * 1024;               // 1GB used (at request limit)
    g_stub_mem_request_quota = 1024 * 1024 * 1024;      // 1GB request
    g_stub_mem_limit_quota = 2ULL * 1024 * 1024 * 1024; // 2GB limit
    g_stub_dynamic_free = 100 * 1024 * 1024;            // 100MB dynamic free

    size_t requested = 512 * 1024 * 1024; // 512MB (need to borrow 512MB, dynamic free only has 100MB)
    bool result = memory_check_elastic(requested);

    EXPECT_FALSE(result);
}

TEST_F(MemLimiterElasticTest, over_limit_returns_false)
{
    g_stub_mem_used = 1800 * 1024 * 1024;               // 1.8GB used
    g_stub_mem_request_quota = 1024 * 1024 * 1024;      // 1GB request
    g_stub_mem_limit_quota = 2ULL * 1024 * 1024 * 1024; // 2GB limit
    g_stub_dynamic_free = 1024 * 1024 * 1024;           // 1GB dynamic free

    size_t requested = 300 * 1024 * 1024; // 300MB (1800 + 300 = 2100MB > 2GB limit)
    bool result = memory_check_elastic(requested);

    EXPECT_FALSE(result);
}

TEST_F(MemLimiterElasticTest, exactly_at_limit_returns_true)
{
    g_stub_mem_used = 1024 * 1024 * 1024;               // 1GB used
    g_stub_mem_request_quota = 1024 * 1024 * 1024;      // 1GB request
    g_stub_mem_limit_quota = 2ULL * 1024 * 1024 * 1024; // 2GB limit
    g_stub_dynamic_free = 1024 * 1024 * 1024;           // 1GB dynamic free

    size_t requested = 1024 * 1024 * 1024; // 1GB (1GB + 1GB = 2GB = exactly at limit)
    bool result = memory_check_elastic(requested);

    EXPECT_TRUE(result);
}

TEST_F(MemLimiterElasticTest, zero_request_quota_falls_back_to_limit_check)
{
    g_stub_mem_used = 100 * 1024 * 1024;                // 100MB used
    g_stub_mem_request_quota = 0;                       // request quota is 0
    g_stub_mem_limit_quota = 2ULL * 1024 * 1024 * 1024; // 2GB limit

    size_t requested = 500 * 1024 * 1024; // 500MB
    bool result = memory_check_elastic(requested);

    EXPECT_TRUE(result);
}

TEST_F(MemLimiterElasticTest, get_mem_used_failure_returns_false)
{
    g_stub_get_mem_used_ret = -1;

    size_t requested = 100 * 1024 * 1024;
    bool result = memory_check_elastic(requested);

    EXPECT_FALSE(result);
}

TEST_F(MemLimiterElasticTest, request_exactly_equals_dynamic_free_returns_true)
{
    g_stub_mem_used = 1024 * 1024 * 1024;               // 1GB used (at request limit)
    g_stub_mem_request_quota = 1024 * 1024 * 1024;      // 1GB request
    g_stub_mem_limit_quota = 3ULL * 1024 * 1024 * 1024; // 3GB limit

    size_t requested = 512 * 1024 * 1024; // 512MB (borrow = 512MB, exactly equals dynamic_free)
    bool result = memory_check_elastic(requested);

    EXPECT_TRUE(result);
}

TEST_F(MemLimiterElasticTest, borrow_one_byte_over_dynamic_free_returns_false)
{
    g_stub_mem_used = 1024 * 1024 * 1024;               // 1GB used (at request limit)
    g_stub_mem_request_quota = 1024 * 1024 * 1024;      // 1GB request
    g_stub_mem_limit_quota = 3ULL * 1024 * 1024 * 1024; // 3GB limit
    g_stub_dynamic_free = 512 * 1024 * 1024;            // 512MB dynamic free

    size_t requested = 512 * 1024 * 1024 + 1; // 512MB + 1 byte (borrow > dynamic_free)
    bool result = memory_check_elastic(requested);

    EXPECT_FALSE(result);
}