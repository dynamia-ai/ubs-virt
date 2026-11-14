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

#include <acl/acl.h>
#include <gtest/gtest.h>
#include <runtime/rt.h>
#include <sys/file.h>
#include <mockcpp/mockcpp.hpp>
#include "dcmi_wrapper.h"
#include "log.h"
#include "mem_limiter.h"
#include "npu_manager.h"
#include "runtime_stub.h"
#include "securec.h"

extern "C" {
static int stub_memory_resource_info(int logic_id, int card_id, int device_id, size_t *used)
{
    (void)logic_id;
    (void)card_id;
    (void)device_id;
    *used = 0;
    return ENPU_SUCCESS;
}

int stub_get_mem_used_huge(size_t *used)
{
    *used = SIZE_MAX;
    return ENPU_SUCCESS;
}
}

class MemoryTest : public testing::Test {
protected:
    static void SetUpTestCase()
    {
        std::cout << "Memory test start" << std::endl;
    }

    static void TearDownTestCase()
    {
        std::cout << "Memory test end" << std::endl;
    }

    void SetUp()
    {
        (void)sprintf_s(g_log_config.log_dir, sizeof(g_log_config.log_dir), "%s", "../__build/log/enpu/");
        fd_ = open(stub_lock_path(), O_CREAT | O_RDONLY, 0755); // ut memctl.lock文件,设置为777权限
        MOCKER(lock_path).stubs().will(invoke(stub_lock_path));
        MOCKER(enpu_load_config).stubs().will(invoke(stub_enpu_load_config));
        MOCKER(enpu_dcmi_get_device_resource_info).stubs().will(invoke(stub_memory_resource_info));
        enpu_global_init();
        MOCKER(load_rt_libraries).stubs().will(invoke(stub_load_rt_libraries));
    }

    void TearDown()
    {
        GlobalMockObject::verify();
        GlobalMockObject::reset();
        close(fd_);
        fd_ = -1;
    }

private:
    int fd_ = -1;
};

TEST_F(MemoryTest, rtMalloc)
{
    constexpr uint32_t MAX_ARR_SIZE = 20;
    void *devPtrsArr[MAX_ARR_SIZE] = {nullptr};
    uint64_t size = 0;
    rtMemType_t type = 0;
    uint16_t moduleId = 0;
    rtError_t error = rtMalloc(devPtrsArr, size, type, moduleId);
    EXPECT_EQ(error, RT_ERROR_NONE);
    size = MAX_ARR_SIZE;
    error = rtMalloc(devPtrsArr, size, type, moduleId);
    EXPECT_EQ(error, ACL_ERROR_STORAGE_OVER_LIMIT);
}

TEST_F(MemoryTest, aclrtMallocImpl)
{
    constexpr uint32_t MAX_ARR_SIZE = 20;
    void *devPtrsArr[MAX_ARR_SIZE] = {nullptr};
    uint64_t size = 0;
    aclrtMemMallocPolicy policy = 0;
    rtError_t error = aclrtMallocImpl(devPtrsArr, size, policy);
    EXPECT_EQ(error, RT_ERROR_NONE);
    size = MAX_ARR_SIZE;
    error = aclrtMallocImpl(devPtrsArr, size, policy);
    EXPECT_EQ(error, ACL_ERROR_STORAGE_OVER_LIMIT);
}

TEST_F(MemoryTest, aclrtMallocAlign32Impl)
{
    constexpr uint32_t MAX_ARR_SIZE = 20;
    void *devPtrsArr[MAX_ARR_SIZE] = {nullptr};
    uint64_t size = 0;
    aclrtMemMallocPolicy policy = 0;
    rtError_t error = aclrtMallocAlign32Impl(devPtrsArr, size, policy);
    EXPECT_EQ(error, RT_ERROR_NONE);
    size = MAX_ARR_SIZE;
    error = aclrtMallocAlign32Impl(devPtrsArr, size, policy);
    EXPECT_EQ(error, ACL_ERROR_STORAGE_OVER_LIMIT);
}

TEST_F(MemoryTest, rtMallocCached)
{
    constexpr uint32_t MAX_ARR_SIZE = 20;
    void *devPtrsArr[MAX_ARR_SIZE] = {nullptr};
    uint64_t size = 0;
    rtMemType_t type = 0;
    uint16_t moduleId = 0;
    rtError_t error = rtMallocCached(devPtrsArr, size, type, moduleId);
    EXPECT_EQ(error, RT_ERROR_NONE);
}

TEST_F(MemoryTest, aclrtMallocCachedImpl)
{
    constexpr uint32_t MAX_ARR_SIZE = 20;
    void *devPtrsArr[MAX_ARR_SIZE] = {nullptr};
    uint64_t size = 0;
    aclrtMemMallocPolicy policy = 0;
    rtError_t error = aclrtMallocCachedImpl(devPtrsArr, size, policy);
    EXPECT_EQ(error, RT_ERROR_NONE);
}

TEST_F(MemoryTest, rtDvppMalloc)
{
    constexpr uint32_t MAX_ARR_SIZE = 20;
    void *devPtrsArr[MAX_ARR_SIZE] = {nullptr};
    uint64_t size = 0;
    uint16_t moduleId = 0;
    rtError_t error = rtDvppMalloc(devPtrsArr, size, moduleId);
    EXPECT_EQ(error, RT_ERROR_NONE);
}

TEST_F(MemoryTest, rtDvppMallocWithFlag)
{
    constexpr uint32_t MAX_ARR_SIZE = 20;
    void *devPtrsArr[MAX_ARR_SIZE] = {nullptr};
    uint64_t size = 0;
    uint16_t moduleId = 0;
    uint32_t flag = 1;
    rtError_t error = rtDvppMallocWithFlag(devPtrsArr, size, flag, moduleId);
    EXPECT_EQ(error, RT_ERROR_NONE);
}

TEST_F(MemoryTest, rtMemAlloc)
{
    constexpr uint32_t MAX_ARR_SIZE = 20;
    void *devPtrsArr[MAX_ARR_SIZE] = {nullptr};
    rtMallocPolicy policy = RT_MEM_MALLOC_HUGE_FIRST;
    rtMallocAdvise advise = RT_MEM_ADVISE_DVPP;
    rtMallocConfig_t *cfg = nullptr;
    uint64_t size = 0;
    rtError_t error = rtMemAlloc(devPtrsArr, size, policy, advise, cfg);
    EXPECT_EQ(error, RT_ERROR_NONE);
}

TEST_F(MemoryTest, rtMemAllocManaged)
{
    constexpr uint32_t MAX_ARR_SIZE = 20;
    void *devPtrsArr[MAX_ARR_SIZE] = {nullptr};
    uint64_t size = 0;
    uint16_t moduleId = 0;
    uint32_t flag = 1;
    rtError_t error = rtMemAllocManaged(devPtrsArr, size, flag, moduleId);
    EXPECT_EQ(error, RT_ERROR_NONE);
}

TEST_F(MemoryTest, aclrtMemAllocManagedImpl)
{
    constexpr uint32_t MAX_ARR_SIZE = 20;
    void *devPtrsArr[MAX_ARR_SIZE] = {nullptr};
    uint64_t size = 0;
    uint32_t flag = 1;
    rtError_t error = aclrtMemAllocManagedImpl(devPtrsArr, size, flag);
    EXPECT_EQ(error, RT_ERROR_NONE);
}

TEST_F(MemoryTest, rtMallocPhysical)
{
    rtDrvMemHandle *handle = nullptr;
    rtDrvMemProp_t *prop = nullptr;
    uint64_t size = 0;
    uint64_t flags = 1;
    rtError_t error = rtMallocPhysical(handle, size, prop, flags);
    EXPECT_EQ(error, RT_ERROR_NONE);
}

// Non-device memory skips the swap path and goes straight to the driver.
TEST_F(MemoryTest, rtMallocPhysicalNonDeviceSide)
{
    rtDrvMemHandle handle = nullptr;
    rtDrvMemProp_t prop;
    prop.side = 1; // non-device, skip swap path
    uint64_t size = 0;
    uint64_t flags = 1;
    rtError_t error = rtMallocPhysical(&handle, size, &prop, flags);
    EXPECT_EQ(error, RT_ERROR_NONE);
}

TEST_F(MemoryTest, aclrtMallocPhysicalImpl)
{
    rtDrvMemHandle *handle = nullptr;
    const aclrtPhysicalMemProp *prop = nullptr;
    uint64_t size = 0;
    uint64_t flags = 1;
    rtError_t error = aclrtMallocPhysicalImpl(handle, size, prop, flags);
    EXPECT_EQ(error, RT_ERROR_NONE);
}

TEST_F(MemoryTest, rtMemGetInfoEx)
{
    rtMemInfoType_t memInfoType = RT_MEMORYINFO_HBM;
    size_t freeSize = 0;
    size_t totalSize = 1;
    rtError_t error = rtMemGetInfoEx(memInfoType, &freeSize, &totalSize);
    EXPECT_EQ(error, RT_ERROR_NONE);
}

TEST_F(MemoryTest, aclrtGetMemInfoImpl)
{
    aclrtMemAttr memInfoType = 0;
    size_t freeSize = 0;
    size_t totalSize = 1;
    rtError_t error = aclrtGetMemInfoImpl(memInfoType, &freeSize, &totalSize);
    EXPECT_EQ(error, RT_ERROR_NONE);
}

TEST_F(MemoryTest, rtMemGetInfoEx_FailTest)
{
    static constexpr int32_t RT_ERROR_INVALID_VALUE = 0x07110001;
    MOCKER(get_mem_used, int(size_t *)).stubs().will(returnValue(-1));
    rtMemInfoType_t memInfoType = RT_MEMORYINFO_HBM;
    size_t freeSize = 0;
    size_t totalSize = 1;
    rtError_t error = rtMemGetInfoEx(memInfoType, &freeSize, &totalSize);
    EXPECT_EQ(error, RT_ERROR_INVALID_VALUE);
}

TEST_F(MemoryTest, aclrtGetMemInfoImpl_FailTest)
{
    static constexpr int32_t RT_ERROR_INVALID_VALUE = 0x07110001;
    MOCKER(get_mem_used, int(size_t *)).stubs().will(returnValue(-1));
    aclrtMemAttr memInfoType = 0;
    size_t freeSize = 0;
    size_t totalSize = 1;
    rtError_t error = aclrtGetMemInfoImpl(memInfoType, &freeSize, &totalSize);
    EXPECT_EQ(error, RT_ERROR_INVALID_VALUE);
}

TEST_F(MemoryTest, rtMemGetInfoEx_NullParams)
{
    rtMemInfoType_t memInfoType = RT_MEMORYINFO_HBM;
    size_t size = 0;
    EXPECT_EQ(rtMemGetInfoEx(memInfoType, nullptr, &size), RT_ERROR_INVALID_VALUE);
    EXPECT_EQ(rtMemGetInfoEx(memInfoType, &size, nullptr), RT_ERROR_INVALID_VALUE);
    EXPECT_EQ(rtMemGetInfoEx(memInfoType, nullptr, nullptr), RT_ERROR_INVALID_VALUE);
}

TEST_F(MemoryTest, rtMemGetInfoEx_UsedExceedsQuota)
{
    MOCKER(get_mem_used, int(size_t *)).stubs().will(invoke(stub_get_mem_used_huge));
    rtMemInfoType_t memInfoType = RT_MEMORYINFO_HBM;
    size_t freeSize = 0;
    size_t totalSize = 0;
    EXPECT_EQ(rtMemGetInfoEx(memInfoType, &freeSize, &totalSize), RT_ERROR_INVALID_VALUE);
}
