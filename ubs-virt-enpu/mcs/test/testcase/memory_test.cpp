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
#include "runtime_stub.h"

extern "C" {
void activate_mem_hooks(void);
rtError_t rtMalloc(void **devPtr, uint64_t size, rtMemType_t type, const uint16_t moduleId);
rtError_t rtMallocCached(void **devPtr, uint64_t size, rtMemType_t type, const uint16_t moduleId);
rtError_t rtDvppMalloc(void **devPtr, uint64_t size, uint16_t moduleId);
rtError_t rtDvppMallocWithFlag(void **devPtr, uint64_t size, uint32_t flag, const uint16_t moduleId);
rtError_t rtMemAlloc(void **devPtr, uint64_t size, rtMallocPolicy policy, rtMallocAdvise advise, rtMallocConfig_t *cfg);
rtError_t rtMemAllocManaged(void **ptr, uint64_t size, uint32_t flag, const uint16_t moduleId);
rtError_t rtMallocPhysical(rtDrvMemHandle *handle, size_t size, rtDrvMemProp_t *prop, uint64_t flags);
rtError_t rtFree(void *devPtr);
rtError_t rtDvppFree(void *devPtr);
rtError_t rtMemFree(void *devPtr);
rtError_t rtMemGetInfoEx(rtMemInfoType_t memInfoType, size_t *freeSize, size_t *totalSize);
}

namespace {
constexpr size_t kMb = 1024UL * 1024UL;

size_t Used()
{
    size_t used = 0;
    EXPECT_EQ(get_mem_used(&used), 0);
    return used;
}

class MemoryHooksTest : public testing::Test {
protected:
    void SetUp() override
    {
        reset_runtime_stub();
        g_mcs_config.memory_quota = 1;
        on_mem_free(Used());
        activate_mem_hooks();
    }
};
} // namespace

TEST(EarlyMemoryHooksTest, PassesThroughBeforeActivation)
{
    reset_runtime_stub();
    g_mcs_config.memory_quota = 1;
    g_stub_malloc_return = 23;
    void *ptr = nullptr;
    EXPECT_EQ(rtMalloc(&ptr, kMb * 2, 0, 0), 23);
    EXPECT_NE(ptr, nullptr);
    EXPECT_EQ(Used(), 0U);

    g_stub_free_return = 24;
    EXPECT_EQ(rtFree(ptr), 24);
    size_t freeSize = 0;
    size_t totalSize = 0;
    EXPECT_EQ(rtMemGetInfoEx(0, &freeSize, &totalSize), 23);
    EXPECT_EQ(freeSize, 4096U);
    EXPECT_EQ(totalSize, 8192U);
}

TEST(EarlyMemoryHooksTest, ReturnsErrorWhenRuntimeSymbolIsMissing)
{
    g_stub_symbols_available = 0;
    void *ptr = nullptr;
    EXPECT_EQ(rtMallocCached(&ptr, 1, 0, 0), -1);
}

TEST_F(MemoryHooksTest, EnforcesQuotaBeforeCallingRuntime)
{
    void *ptr = reinterpret_cast<void *>(0x1);
    EXPECT_EQ(rtMalloc(&ptr, kMb + 1, 0, 0), ACL_ERROR_STORAGE_OVER_LIMIT);
    EXPECT_EQ(ptr, reinterpret_cast<void *>(0x1));
    EXPECT_EQ(Used(), 0U);
    EXPECT_EQ(rtMalloc(&ptr, kMb, 0, 0), 0);
    EXPECT_EQ(Used(), kMb);
    EXPECT_EQ(rtFree(ptr), 0);
    EXPECT_EQ(Used(), 0U);
}

TEST_F(MemoryHooksTest, DoesNotChargeFailedOrInvalidAllocations)
{
    g_stub_malloc_return = 31;
    void *ptr = nullptr;
    EXPECT_EQ(rtMalloc(&ptr, 128, 0, 0), 31);
    EXPECT_EQ(Used(), 0U);

    g_stub_malloc_return = 0;
    EXPECT_EQ(rtMalloc(nullptr, 128, 0, 0), 0);
    EXPECT_EQ(Used(), 0U);

    ptr = nullptr;
    EXPECT_EQ(rtMalloc(&ptr, 128, 0, 0), 0);
    ASSERT_NE(ptr, nullptr);
    g_stub_free_return = 32;
    EXPECT_EQ(rtFree(ptr), 32);
    EXPECT_EQ(Used(), 128U);
    g_stub_free_return = 0;
    EXPECT_EQ(rtFree(ptr), 0);
    EXPECT_EQ(Used(), 0U);
}

TEST_F(MemoryHooksTest, TracksEveryAllocationAndMatchingFreeEntryPoint)
{
    void *mallocPtr = nullptr;
    void *cachedPtr = nullptr;
    void *dvppPtr = nullptr;
    void *dvppFlagPtr = nullptr;
    void *memPtr = nullptr;
    void *managedPtr = nullptr;
    EXPECT_EQ(rtMalloc(&mallocPtr, 128, 0, 0), 0);
    EXPECT_EQ(rtMallocCached(&cachedPtr, 256, 0, 0), 0);
    EXPECT_EQ(rtDvppMalloc(&dvppPtr, 512, 0), 0);
    EXPECT_EQ(rtDvppMallocWithFlag(&dvppFlagPtr, 1024, 1, 0), 0);
    EXPECT_EQ(rtMemAlloc(&memPtr, 2048, 0, 0, nullptr), 0);
    EXPECT_EQ(rtMemAllocManaged(&managedPtr, 4096, 0, 0), 0);
    EXPECT_EQ(Used(), 128U + 256U + 512U + 1024U + 2048U + 4096U);

    EXPECT_EQ(rtFree(mallocPtr), 0);
    EXPECT_EQ(rtFree(cachedPtr), 0);
    EXPECT_EQ(rtDvppFree(dvppPtr), 0);
    EXPECT_EQ(rtDvppFree(dvppFlagPtr), 0);
    EXPECT_EQ(rtMemFree(memPtr), 0);
    EXPECT_EQ(rtMemFree(managedPtr), 0);
    EXPECT_EQ(Used(), 0U);

    EXPECT_EQ(rtFree(mallocPtr), 0);
    EXPECT_EQ(Used(), 0U);
    EXPECT_EQ(rtFree(nullptr), 0);
}

TEST_F(MemoryHooksTest, TracksPhysicalHandleAllocation)
{
    rtDrvMemHandle handle = 0;
    rtDrvMemProp_t prop = 0;
    EXPECT_EQ(rtMallocPhysical(&handle, 8192, &prop, 0), 0);
    EXPECT_NE(handle, 0);
    EXPECT_EQ(Used(), 8192U);
}

TEST_F(MemoryHooksTest, ReportsVirtualQuotaThroughMemGetInfo)
{
    void *ptr = nullptr;
    ASSERT_EQ(rtMalloc(&ptr, 1000, 0, 0), 0);
    size_t freeSize = 0;
    size_t totalSize = 0;
    EXPECT_EQ(rtMemGetInfoEx(0, &freeSize, &totalSize), 0);
    EXPECT_EQ(totalSize, kMb);
    EXPECT_EQ(freeSize, kMb - 1000);
    EXPECT_EQ(rtFree(ptr), 0);

    g_mcs_config.memory_quota = 0;
    EXPECT_EQ(rtMemGetInfoEx(0, &freeSize, &totalSize), 0);
    EXPECT_EQ(freeSize, 0U);
    EXPECT_EQ(totalSize, 0U);
}
