/*
 * Copyright (c) Huawei Technologies Co., Ltd. 2026-2026. All rights reserved.
 * ubs-virt-enpu is licensed under Mulan PSL v2.
 * You may use this software according to the terms and conditions of the Mulan PSL v2.
 * You may obtain a copy of Mulan PSL v2 at:
 *          http://license.coscl.org.cn/MulanPSL2
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND,
 * EITHER EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT,
 * MERCHANTABILITY OR FIT FOR A PARTICULAR PURPOSE.
 * See the Mulan PSL v2 for more details.
 */

/* mem-swap 模块 gtest 用例: 深路径覆盖(swap_hook 全链路/swap_monitor 线程/swap_buffer_shm/
 * memory_tracker map-handle 记录族/拦截接口透传). 通过真实 POSIX shm 文件构造共享状态,
 * 不依赖独立单测的 mock 桩体系. 本套件须位于 VNPU_TESTCASE_FILES 末尾: 其会重置
 * g_shm_state_init/post_init_flag 并改写内存配额, 避免影响先前的用例. */
#include <fcntl.h>
#include <gtest/gtest.h>
#include <pthread.h>
#include <runtime/rt.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>
#include <cstdint>
#include <cstring>
#include <iostream>
#include "dcmi_wrapper.h"
#include "log.h"
#include "memory_tracker.h"
#include "npu_manager.h"
#include "runtime_hook.h"
#include "securec.h"
#include "shm_manager.h"
#include "swap_buffer_shm.h"
#include "swap_executor.h"
#include "swap_hook.h"
#include "swap_monitor_thread.h"

/* 桩函数表 RUNTIME_FUNCTION_LIST 未声明的 swap 拦截接口, 按 src/ascend/memory.c 原型补声明 */
extern "C" {
rtError_t rtFree(void *devPtr);
rtError_t rtFreePhysical(rtDrvMemHandle handle);
rtError_t rtMapMem(void *devPtr, size_t size, size_t offset, rtDrvMemHandle handle, uint64_t flags);
rtError_t rtUnmapMem(void *devPtr);
rtError_t rtMemcpy(void *dst, uint64_t destMax, const void *src, uint64_t cnt, rtMemcpyKind_t kind);
rtError_t rtsPointerGetAttributes(const void *ptr, rtPtrAttributes_t *attributes);
rtError_t rtMemcpyAsync(void *dst, uint64_t destMax, const void *src, uint64_t cnt, rtMemcpyKind_t kind,
                        rtStream_t stm);
rtError_t rtsCheckMemType(void **addrs, uint32_t size, uint32_t memType, uint32_t *checkResult, uint32_t reserve);
rtError_t rtMemcpyAsyncEx(void *dst, uint64_t destMax, const void *src, uint64_t cnt, rtMemcpyKind_t kind,
                          rtStream_t stm, rtMemcpyConfig_t *memcpyConfig);
rtError_t rtsMemcpyBatch(void **dsts, void **srcs, size_t *sizes, size_t count, rtMemcpyBatchAttr *attrs,
                         size_t *attrsIdxs, size_t numAttrs, size_t *failIdx);
rtError_t rtsMemcpyBatchAsync(void **dsts, size_t *destMaxs, void **srcs, size_t *sizes, size_t count,
                              rtMemcpyBatchAttr *attrs, size_t *attrsIdxs, size_t numAttrs, size_t *failIdx,
                              rtStream_t stream);
rtError_t rtMemcpy2d(void *dst, uint64_t dstPitch, const void *src, uint64_t srcPitch, uint64_t width, uint64_t height,
                     rtMemcpyKind_t kind);
rtError_t rtMemcpy2dAsync(void *dst, uint64_t dstPitch, const void *src, uint64_t srcPitch, uint64_t width,
                          uint64_t height, rtMemcpyKind_t kind, rtStream_t stm);
rtError_t rtsSetMemcpyDesc(rtMemcpyDesc_t desc, rtMemcpyKind kind, void *srcAddr, void *dstAddr, size_t count,
                           rtMemcpyConfig_t *config);
rtError_t rtsMemcpyAsyncWithDesc(rtMemcpyDesc_t desc, rtMemcpyKind kind, rtMemcpyConfig_t *config, rtStream_t stream);
rtError_t rtMemcpyAsyncWithOffset(void **dst, uint64_t dstMax, uint64_t dstDataOffset, const void **src, uint64_t cnt,
                                  uint64_t srcDataOffset, rtMemcpyKind kind, rtStream_t stm);
rtError_t rtMemset(void *devPtr, uint64_t destMax, uint32_t val, uint64_t cnt);
rtError_t rtMemsetAsync(void *ptr, uint64_t destMax, uint32_t val, uint64_t cnt, rtStream_t stm);
rtError_t rtMemPrefetchToDevice(void *devPtr, uint64_t len, int32_t devId);
rtError_t rtsIpcMemGetExportKey(const void *ptr, size_t size, char_t *key, uint32_t len, uint64_t flags);
rtError_t rtsIpcMemImportByKey(void **ptr, const char_t *key, uint64_t flags);
rtError_t rtsValueWrite(const void *const devAddr, const uint64_t value, const uint32_t flag, rtStream_t stm);
rtError_t rtsValueWait(const void *const devAddr, const uint64_t value, const uint32_t flag, rtStream_t stm);

/* npu_manager.c 中非 static 的惰性初始化标志, 重置后可使 get_shm_state 重新 attach */
extern bool g_shm_state_init;
extern pthread_once_t post_init_flag;
}

namespace {
constexpr int kVnpuId = 0;
constexpr int kPhyId = 0;
constexpr uint64_t kHbmTotalMb = 65536; /* 64GB, 单位 MB */
constexpr size_t kSwapBufferSize = 1UL * 1024 * 1024;
constexpr uint64_t kTestRequestMb = 1024; /* request != limit, 使能 swap */
constexpr uint64_t kTestLimitMb = 2048;
const char *kDieId = "AAAAAAAA-BBBBBBBB-CCCCCCCC-DDDDDDDD-EEEEEEEE"; /* 与桩配置一致 */

/* RUNTIME_FUNCTION_LIST 未覆盖的 swap 拦截符号, 测试进程内补齐函数表项.
 * 输出参数由桩写入, 保证 swap_executor 的换入换出路径可完整执行. */
rtError_t stub_swap_get_granularity(rtDrvMemProp_t *prop, uint64_t flags, size_t *granularity)
{
    (void)prop;
    (void)flags;
    if (granularity != nullptr) {
        *granularity = 1; /* 粒度为 1, 对齐后 size 保持不变 */
    }
    return RT_ERROR_NONE;
}

rtError_t stub_swap_malloc_physical(rtDrvMemHandle *handle, size_t size, rtDrvMemProp_t *prop, uint64_t flags)
{
    (void)size;
    (void)prop;
    (void)flags;
    if (handle != nullptr) {
        *handle = (rtDrvMemHandle)(intptr_t)0x3000;
    }
    return RT_ERROR_NONE;
}

rtError_t stub_swap_reserve_address(void **ptr, size_t size, uint32_t offset, void *processVa, uint64_t flags)
{
    (void)size;
    (void)offset;
    (void)processVa;
    (void)flags;
    if (ptr != nullptr) {
        *ptr = (void *)(intptr_t)0x5000;
    }
    return RT_ERROR_NONE;
}

rtError_t stub_swap_ok_no_arg(void)
{
    return RT_ERROR_NONE;
}

rtError_t stub_swap_rt_free(void *devPtr)
{
    (void)devPtr;
    return RT_ERROR_NONE;
}

rtError_t stub_swap_rt_memcpy(void *dst, uint64_t destMax, const void *src, uint64_t cnt, rtMemcpyKind_t kind)
{
    (void)dst;
    (void)destMax;
    (void)src;
    (void)cnt;
    (void)kind;
    return RT_ERROR_NONE;
}

void fill_swap_rt_entries()
{
    /* 输出型桩: 粒度/物理句柄/虚拟地址 */
    rt_library_entry[HOOK_rtMemGetAllocationGranularity].func_ptr = (void *)stub_swap_get_granularity;
    rt_library_entry[HOOK_rtMallocPhysical].func_ptr = (void *)stub_swap_malloc_physical;
    rt_library_entry[HOOK_rtReserveMemAddress].func_ptr = (void *)stub_swap_reserve_address;
    /* 透传型桩 */
    rt_library_entry[HOOK_rtFree].func_ptr = (void *)stub_swap_rt_free;
    rt_library_entry[HOOK_rtFreePhysical].func_ptr = (void *)stub_swap_rt_free;
    rt_library_entry[HOOK_rtMapMem].func_ptr = (void *)stub_swap_ok_no_arg;
    rt_library_entry[HOOK_rtUnmapMem].func_ptr = (void *)stub_swap_rt_free;
    rt_library_entry[HOOK_rtReleaseMemAddress].func_ptr = (void *)stub_swap_rt_free;
    rt_library_entry[HOOK_rtMemcpy].func_ptr = (void *)stub_swap_rt_memcpy;
    rt_library_entry[HOOK_rtMemcpyAsync].func_ptr = (void *)stub_swap_ok_no_arg;
    rt_library_entry[HOOK_rtMemcpyAsyncEx].func_ptr = (void *)stub_swap_ok_no_arg;
    rt_library_entry[HOOK_rtMemcpy2d].func_ptr = (void *)stub_swap_ok_no_arg;
    rt_library_entry[HOOK_rtMemcpy2dAsync].func_ptr = (void *)stub_swap_ok_no_arg;
    rt_library_entry[HOOK_rtMemcpyAsyncWithOffset].func_ptr = (void *)stub_swap_ok_no_arg;
    rt_library_entry[HOOK_rtsMemcpyBatch].func_ptr = (void *)stub_swap_ok_no_arg;
    rt_library_entry[HOOK_rtsMemcpyBatchAsync].func_ptr = (void *)stub_swap_ok_no_arg;
    rt_library_entry[HOOK_rtsMemcpyAsyncWithDesc].func_ptr = (void *)stub_swap_ok_no_arg;
    rt_library_entry[HOOK_rtsSetMemcpyDesc].func_ptr = (void *)stub_swap_ok_no_arg;
    rt_library_entry[HOOK_rtsCheckMemType].func_ptr = (void *)stub_swap_ok_no_arg;
    rt_library_entry[HOOK_rtsPointerGetAttributes].func_ptr = (void *)stub_swap_ok_no_arg;
    rt_library_entry[HOOK_rtMemset].func_ptr = (void *)stub_swap_ok_no_arg;
    rt_library_entry[HOOK_rtMemsetAsync].func_ptr = (void *)stub_swap_ok_no_arg;
    rt_library_entry[HOOK_rtMemPrefetchToDevice].func_ptr = (void *)stub_swap_ok_no_arg;
    rt_library_entry[HOOK_rtsIpcMemGetExportKey].func_ptr = (void *)stub_swap_ok_no_arg;
    rt_library_entry[HOOK_rtsIpcMemImportByKey].func_ptr = (void *)stub_swap_ok_no_arg;
    rt_library_entry[HOOK_rtsValueWrite].func_ptr = (void *)stub_swap_ok_no_arg;
    rt_library_entry[HOOK_rtsValueWait].func_ptr = (void *)stub_swap_ok_no_arg;
}

/* 创建共享内存形态的 shm_state, 供 get_shm_state 真实 attach */
shm_state_t *create_state_shm(const char *dieId, int phyId, bool versionOk)
{
    char shmName[128];
    int ret = snprintf_s(shmName, sizeof(shmName), sizeof(shmName) - 1, "/shm_state-%d-%s", phyId, dieId);
    if (ret < 0) {
        return nullptr;
    }
    int fd = shm_open(shmName, O_CREAT | O_RDWR | O_TRUNC, 0644);
    if (fd < 0) {
        return nullptr;
    }
    if (ftruncate(fd, static_cast<off_t>(sizeof(shm_state_t))) != 0) {
        close(fd);
        return nullptr;
    }
    void *addr = mmap(nullptr, sizeof(shm_state_t), PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
    close(fd);
    if (addr == MAP_FAILED) {
        return nullptr;
    }
    shm_state_t *state = static_cast<shm_state_t *>(addr);
    (void)memset_s(state, sizeof(shm_state_t), 0, sizeof(shm_state_t));
    state->version = versionOk ? SHM_STATE_VERSION : (SHM_STATE_VERSION + 1);
    state->phy_id = phyId;
    state->hbm_total = kHbmTotalMb * MB_TO_B;
    vnpu_bitmap_set(state->vnpu_bitmap, kVnpuId);
    state->entries[kVnpuId].vnpu_id = kVnpuId;
    state->entries[kVnpuId].hbm_request = kTestRequestMb;
    state->entries[kVnpuId].hbm_limit = kTestLimitMb;
    /* 单容器多进程: 进程槽位由 memset 清零(active=false), ID 计数器从 1 开始 */
    state->next_vcann_rt_id.store(1);
    pthread_mutex_init(&state->swap_in_mutex, nullptr);
    state->initialized = true;
    return state;
}

void create_swap_buffer_shm()
{
    int fd = shm_open(SHM_SWAP_BUFFER_NAME, O_CREAT | O_RDWR | O_TRUNC, 0644);
    if (fd < 0) {
        return;
    }
    (void)ftruncate(fd, static_cast<off_t>(kSwapBufferSize));
    close(fd);
}

int g_iterate_sum = 0;
void iterate_callback(memory_record_t *record, void *ctx)
{
    (void)ctx;
    g_iterate_sum += static_cast<int>(record->size);
}
} // namespace

class SwapModuleTest : public testing::Test {
protected:
    static void SetUpTestCase()
    {
        std::cout << "Swap module test start" << std::endl;
        (void)sprintf_s(g_log_config.log_dir, sizeof(g_log_config.log_dir), "%s", "../__build/log/enpu/");
        (void)log_init();
        fill_swap_rt_entries();
        create_swap_buffer_shm();
        state_shm_ = create_state_shm(kDieId, kPhyId, true);
        ASSERT_NE(state_shm_, nullptr);
        /* 固定本进程的 vNPU/shm_id, 使 get_vnpu_id() 与本套件构造的 state 一致 */
        config.phy_npu_id = kPhyId;
        config.vnpu_id = kVnpuId;
        config.scheduling_policy = SCHED_POLICY_FIXED_SHARE;
        config.aicore_quota = DEFAULT_AICORE_NUM;
        config.memory_request = 0;
        config.memory_limit = 0;
        config.memory_quota = static_cast<int32_t>(kHbmTotalMb);
        ASSERT_EQ(strcpy_s(config.shm_id, sizeof(config.shm_id), kDieId), 0);
        ASSERT_EQ(enpu_config_info_init(), ENPU_SUCCESS);
        /* 重置惰性标志后由真实代码完成 attach */
        g_shm_state_init = false;
        set_mem_request_quota(kTestRequestMb * MB_TO_B);
        set_mem_limit_quota(kTestLimitMb * MB_TO_B);
        attached_ = get_shm_state();
        ASSERT_NE(attached_, nullptr);
        ASSERT_EQ(attached_->hbm_total, kHbmTotalMb * MB_TO_B);
        /* 单容器多进程: 显式注册本进程, 取得进程级换出槽位 */
        ASSERT_EQ(register_to_shm_state(attached_, kVnpuId), ENPU_SUCCESS);
        ASSERT_GE(get_registry_slot(), 0);
    }

    static void TearDownTestCase()
    {
        std::cout << "Swap module test end" << std::endl;
        swap_hook_global_destroy();
        shm_unlink(SHM_SWAP_BUFFER_NAME);
        char shmName[128];
        (void)snprintf_s(shmName, sizeof(shmName), sizeof(shmName) - 1, "/shm_state-%d-%s", kPhyId, kDieId);
        shm_unlink(shmName);
        shm_unlink("/shm_state-1-version-bad");
        if (state_shm_ != nullptr) {
            munmap(state_shm_, sizeof(shm_state_t));
            state_shm_ = nullptr;
        }
    }

    void SetUp() {}
    void TearDown() {}

    static shm_state_t *state_shm_;
    static shm_state_t *attached_;
};

shm_state_t *SwapModuleTest::state_shm_ = nullptr;
shm_state_t *SwapModuleTest::attached_ = nullptr;

/* ---------------- memory_tracker: 记录族与容量扩展 ---------------- */

TEST_F(SwapModuleTest, TrackerExpandByAdd)
{
    memory_tracker_t *tracker = memory_tracker_create(2);
    ASSERT_NE(tracker, nullptr);
    /* 容量 2 时插入 1 条即触发扩容(容量 <= 2*count) */
    EXPECT_EQ(memory_tracker_add(tracker, (void *)(intptr_t)0x100, 64, (rtDrvMemHandle)(intptr_t)0x200, false),
              ENPU_SUCCESS);
    EXPECT_GE(tracker->capacity, (size_t)4);
    EXPECT_EQ(memory_tracker_find(tracker, (void *)(intptr_t)0x100)->size, (uint64_t)64);
    /* 直接调用扩容: 仅重建数组容量 */
    EXPECT_EQ(memory_tracker_expand(tracker, 2), ENPU_SUCCESS);
    EXPECT_EQ(memory_tracker_expand(tracker, 0), ENPU_FAIL);
    EXPECT_EQ(memory_tracker_destroy(tracker), ENPU_SUCCESS);
}

TEST_F(SwapModuleTest, TrackerIterateAndCollect)
{
    memory_tracker_t *tracker = memory_tracker_create(8);
    ASSERT_NE(tracker, nullptr);
    EXPECT_EQ(memory_tracker_add(tracker, (void *)(intptr_t)0x101, 32, (rtDrvMemHandle)(intptr_t)0x201, false),
              ENPU_SUCCESS);
    EXPECT_EQ(memory_tracker_add(tracker, (void *)(intptr_t)0x102, 96, (rtDrvMemHandle)(intptr_t)0x202, false),
              ENPU_SUCCESS);

    g_iterate_sum = 0;
    /* iterate 当前为未实现的存根, 恒返回失败 */
    EXPECT_EQ(memory_tracker_iterate(tracker, iterate_callback, nullptr), ENPU_FAIL);
    EXPECT_EQ(g_iterate_sum, 0);
    EXPECT_EQ(memory_tracker_iterate(nullptr, iterate_callback, nullptr), ENPU_FAIL);

    memory_record_t **records = nullptr;
    int count = 0;
    EXPECT_EQ(memory_tracker_collect_records(tracker, &records, &count), ENPU_SUCCESS);
    ASSERT_NE(records, nullptr);
    EXPECT_EQ(count, 2);
    free(records);
    EXPECT_EQ(memory_tracker_collect_records(nullptr, &records, &count), ENPU_FAIL);
    EXPECT_EQ(memory_tracker_collect_records(tracker, nullptr, &count), ENPU_FAIL);
    EXPECT_EQ(memory_tracker_collect_records(tracker, &records, nullptr), ENPU_FAIL);

    EXPECT_EQ(memory_tracker_find_swapped(tracker), nullptr);
    EXPECT_EQ(memory_tracker_mark_swapped(tracker, (void *)(intptr_t)0x101, 4096), ENPU_SUCCESS);
    memory_record_t *swapped = memory_tracker_find_swapped(tracker);
    ASSERT_NE(swapped, nullptr);
    EXPECT_EQ(swapped->swap_offset, (uint64_t)4096);
    EXPECT_EQ(memory_tracker_find_swapped(nullptr), nullptr);

    EXPECT_EQ(get_record_size(tracker, (void *)(intptr_t)0x101, 1), (uint64_t)32);
    EXPECT_EQ(get_record_size(tracker, (void *)(intptr_t)0x999, 7), (uint64_t)7);
    EXPECT_EQ(get_record_size(nullptr, (void *)(intptr_t)0x101, 9), (uint64_t)9);
    EXPECT_EQ(memory_tracker_destroy(tracker), ENPU_SUCCESS);
}

TEST_F(SwapModuleTest, TrackerMapAndHandleRecords)
{
    memory_tracker_t *tracker = memory_tracker_create(8);
    ASSERT_NE(tracker, nullptr);

    /* map 记录: 增/查/删 */
    EXPECT_EQ(memory_tracker_add_map_record(tracker, (void *)(intptr_t)0x5000, 4096, 256,
                                            (rtDrvMemHandle)(intptr_t)0x3000, 0x11),
              ENPU_SUCCESS);
    EXPECT_EQ(memory_tracker_add_map_record(nullptr, nullptr, 0, 0, nullptr, 0), ENPU_FAIL);
    void *va = nullptr;
    EXPECT_EQ(memory_tracker_get_map_va(tracker, (rtDrvMemHandle)(intptr_t)0x3000, &va), ENPU_SUCCESS);
    EXPECT_EQ(va, (void *)(intptr_t)0x5000);
    size_t size = 0;
    size_t offset = 0;
    uint64_t flags = 0;
    EXPECT_EQ(memory_tracker_get_map_prop(tracker, (void *)(intptr_t)0x5000, &size, &offset, &flags), ENPU_SUCCESS);
    EXPECT_EQ(size, (size_t)4096);
    EXPECT_EQ(offset, (size_t)256);
    EXPECT_EQ(flags, (uint64_t)0x11);
    EXPECT_EQ(memory_tracker_get_map_prop(tracker, (void *)(intptr_t)0x6000, &size, &offset, &flags), ENPU_FAIL);
    EXPECT_EQ(memory_tracker_get_map_va(tracker, (rtDrvMemHandle)(intptr_t)0x9999, &va), ENPU_SUCCESS);
    EXPECT_EQ(va, nullptr);
    EXPECT_EQ(memory_tracker_get_map_va(nullptr, nullptr, &va), ENPU_FAIL);
    EXPECT_EQ(memory_tracker_get_map_prop(nullptr, nullptr, &size, &offset, &flags), ENPU_FAIL);
    EXPECT_EQ(memory_tracker_remove_map_record(tracker, (void *)(intptr_t)0x5000), ENPU_SUCCESS);
    EXPECT_EQ(memory_tracker_get_map_va(tracker, (rtDrvMemHandle)(intptr_t)0x3000, &va), ENPU_SUCCESS);
    EXPECT_EQ(va, nullptr);
    EXPECT_EQ(memory_tracker_remove_map_record(tracker, (void *)(intptr_t)0x5000), ENPU_SUCCESS);
    EXPECT_EQ(memory_tracker_remove_map_record(nullptr, nullptr), ENPU_FAIL);

    /* handle 记录: 增/查/删 */
    rtDrvMemProp_t prop;
    (void)memset_s(&prop, sizeof(prop), 0, sizeof(prop));
    prop.side = ACL_MEM_LOCATION_TYPE_DEVICE;
    prop.devid = 0;
    prop.module_id = 33;
    EXPECT_EQ(memory_tracker_add_handle_record(tracker, (rtDrvMemHandle)(intptr_t)0x3000, 8192, prop, 0x22),
              ENPU_SUCCESS);
    EXPECT_EQ(memory_tracker_add_handle_record(nullptr, nullptr, 0, prop, 0), ENPU_FAIL);
    size = 0;
    rtDrvMemProp_t outProp;
    (void)memset_s(&outProp, sizeof(outProp), 0, sizeof(outProp));
    flags = 0;
    EXPECT_EQ(memory_tracker_get_handle_prop(tracker, (rtDrvMemHandle)(intptr_t)0x3000, &size, &outProp, &flags),
              ENPU_SUCCESS);
    EXPECT_EQ(size, (size_t)8192);
    EXPECT_EQ(flags, (uint64_t)0x22);
    EXPECT_EQ(memory_tracker_get_handle_prop(tracker, (rtDrvMemHandle)(intptr_t)0x9999, &size, &outProp, &flags),
              ENPU_FAIL);
    EXPECT_EQ(memory_tracker_get_handle_prop(nullptr, nullptr, &size, &outProp, &flags), ENPU_FAIL);
    EXPECT_EQ(memory_tracker_remove_handle_record(tracker, (rtDrvMemHandle)(intptr_t)0x3000), ENPU_SUCCESS);
    EXPECT_EQ(memory_tracker_get_handle_prop(tracker, (rtDrvMemHandle)(intptr_t)0x3000, &size, &outProp, &flags),
              ENPU_FAIL);
    EXPECT_EQ(memory_tracker_remove_handle_record(tracker, (rtDrvMemHandle)(intptr_t)0x3000), ENPU_SUCCESS);
    EXPECT_EQ(memory_tracker_remove_handle_record(nullptr, nullptr), ENPU_FAIL);

    EXPECT_EQ(memory_tracker_destroy(tracker), ENPU_SUCCESS);
}

/* ---------------- shm_manager: attach 错误分支与状态操作 ---------------- */

TEST_F(SwapModuleTest, ShmAttachInvalidParams)
{
    EXPECT_EQ(shm_state_posix_shm_attach(kPhyId, nullptr), nullptr);
    EXPECT_EQ(shm_state_posix_shm_attach(kPhyId, ""), nullptr);
    EXPECT_EQ(shm_state_posix_shm_attach(-1, kDieId), nullptr);
    shm_state_posix_shm_detach(nullptr);
    /* 版本不匹配的 shm: 立即失败返回 */
    shm_state_t *bad = create_state_shm("version-bad", 1, false);
    ASSERT_NE(bad, nullptr);
    EXPECT_EQ(shm_state_posix_shm_attach(1, "version-bad"), nullptr);
    munmap(bad, sizeof(shm_state_t));
}

TEST_F(SwapModuleTest, ShmStateApi)
{
    shm_state_t *state = attached_;
    ASSERT_NE(state, nullptr);

    EXPECT_EQ(shm_update_hbm_request(nullptr, kVnpuId, 100), ENPU_FAIL);
    EXPECT_EQ(shm_update_hbm_request(state, kVnpuId, 512), ENPU_SUCCESS);
    EXPECT_EQ(state->entries[kVnpuId].hbm_request, (uint64_t)512);
    EXPECT_EQ(shm_update_hbm_request(state, MAX_VNPU_PER_DIE, 100), ENPU_FAIL);
    EXPECT_EQ(shm_update_hbm_request(state, kVnpuId, kTestRequestMb), ENPU_SUCCESS);

    EXPECT_EQ(shm_update_used(nullptr, kVnpuId, 100), ENPU_FAIL);
    EXPECT_EQ(shm_update_used(state, MAX_VNPU_PER_DIE, 100), ENPU_FAIL);
    EXPECT_EQ(shm_update_used(state, kVnpuId, 100 * MB_TO_B), ENPU_SUCCESS);

    EXPECT_EQ(shm_get_dynamic_free(nullptr), (uint64_t)0);
    EXPECT_EQ(shm_get_dynamic_free(state), (uint64_t)((kHbmTotalMb - kTestRequestMb) * MB_TO_B));
    EXPECT_EQ(shm_get_hbm_request_free(nullptr), (uint64_t)0);
    EXPECT_EQ(shm_get_hbm_request_free(state), (uint64_t)((kHbmTotalMb - kTestRequestMb) * MB_TO_B));

    int32_t cmdVnpu = -1;
    int32_t action = -1;
    EXPECT_EQ(shm_check_swap_cmd(nullptr, &cmdVnpu, &action), -1);
    EXPECT_EQ(shm_check_swap_cmd(state, nullptr, &action), -1);
    EXPECT_EQ(shm_check_swap_cmd(state, &cmdVnpu, nullptr), -1);
    state->swap_cmd.target_vnpu_id = kVnpuId;
    state->swap_cmd.action = SWAP_ACTION_OUT;
    EXPECT_EQ(shm_check_swap_cmd(state, &cmdVnpu, &action), 0);
    EXPECT_EQ(cmdVnpu, kVnpuId);
    EXPECT_EQ(action, SWAP_ACTION_OUT);

    shm_ack_swap_cmd(nullptr);
    state->swap_out_cmd.target_vnpu_id = kVnpuId;
    state->swap_cmd.completed = false;
    state->swap_out_cmd.completed = false;
    shm_ack_swap_cmd(state);
    EXPECT_TRUE(state->swap_cmd.completed);
    EXPECT_TRUE(state->swap_out_cmd.completed);

    /* 进程级 swapped: 越界参数静默忽略, 合法槽位按进程独立维护 */
    int slot = get_registry_slot();
    ASSERT_GE(slot, 0);
    shm_set_process_swapped(nullptr, kVnpuId, slot, true);
    shm_set_process_swapped(state, MAX_VNPU_PER_DIE, slot, true);
    shm_set_process_swapped(state, kVnpuId, MAX_PROCESSES_PER_VNPU, true);
    EXPECT_FALSE(shm_get_process_swapped(state, kVnpuId, slot));
    EXPECT_FALSE(shm_get_vnpu_all_swapped(state, kVnpuId));
    shm_set_process_swapped(state, kVnpuId, slot, true);
    EXPECT_TRUE(shm_get_process_swapped(state, kVnpuId, slot));
    EXPECT_TRUE(shm_get_vnpu_all_swapped(state, kVnpuId));
    shm_set_process_swapped(state, kVnpuId, slot, false);
    EXPECT_FALSE(shm_get_process_swapped(state, kVnpuId, slot));
    EXPECT_FALSE(shm_get_process_swapped(nullptr, kVnpuId, slot));
    EXPECT_FALSE(shm_get_process_swapped(state, MAX_VNPU_PER_DIE, slot));
    /* 非法参数走 fail-closed: 返回 false, 避免调用方误认为该 vNPU 已整体换出 */
    EXPECT_FALSE(shm_get_vnpu_all_swapped(nullptr, kVnpuId));

    /* npu_manager 包装接口: 已挂载状态下走 shm 更新 */
    EXPECT_EQ(update_shm_hbm_request(0), ENPU_SUCCESS);
    EXPECT_EQ(update_shm_used(0), ENPU_SUCCESS);
}

/* ---------------- swap_buffer_shm: attach/detach ---------------- */

TEST_F(SwapModuleTest, SwapBufferAttachDetach)
{
    void *base = swap_buffer_shm_attach();
    ASSERT_NE(base, nullptr);
    EXPECT_EQ(swap_buffer_shm_detach(base), ENPU_SUCCESS);
    /* detach 后记录大小已清零, 再次 detach 同一基址按失败处理 */
    EXPECT_EQ(swap_buffer_shm_detach(base), ENPU_FAIL);
    EXPECT_EQ(swap_buffer_shm_detach(nullptr), ENPU_SUCCESS);
}

/* ---------------- swap_monitor_thread: 参数校验与起停 ---------------- */

TEST_F(SwapModuleTest, MonitorCreateInvalidParams)
{
    shm_state_t *state = attached_;
    swap_executor_t *executor = swap_executor_create((void *)0x7000);
    ASSERT_NE(executor, nullptr);
    memory_tracker_t *tracker = memory_tracker_create(8);
    ASSERT_NE(tracker, nullptr);

    EXPECT_EQ(swap_monitor_thread_create(kVnpuId, nullptr, executor, tracker, 10), nullptr);
    EXPECT_EQ(swap_monitor_thread_create(kVnpuId, state, nullptr, tracker, 10), nullptr);
    EXPECT_EQ(swap_monitor_thread_create(kVnpuId, state, executor, nullptr, 10), nullptr);
    EXPECT_EQ(swap_monitor_thread_create(-1, state, executor, tracker, 10), nullptr);
    EXPECT_EQ(swap_monitor_thread_create(kVnpuId, state, executor, tracker, 0), nullptr);
    swap_monitor_thread_t *redundant = swap_monitor_thread_create(kVnpuId, state, executor, tracker, 10);
    EXPECT_NE(redundant, nullptr);
    EXPECT_EQ(swap_monitor_thread_destroy(redundant), ENPU_SUCCESS);
    EXPECT_EQ(swap_monitor_thread_destroy(nullptr), ENPU_FAIL);

    /* 起停一轮空闲轮询(poll 1ms), 覆盖线程主循环的空转分支 */
    swap_monitor_thread_t *monitor = swap_monitor_thread_create(kVnpuId, state, executor, tracker, 1);
    ASSERT_NE(monitor, nullptr);
    EXPECT_EQ(swap_monitor_thread_start(monitor), ENPU_SUCCESS);
    usleep(50000);
    EXPECT_EQ(swap_monitor_thread_stop(monitor), ENPU_SUCCESS);
    EXPECT_EQ(swap_monitor_thread_destroy(monitor), ENPU_SUCCESS);

    EXPECT_EQ(swap_executor_destroy(executor), ENPU_SUCCESS);
    EXPECT_EQ(memory_tracker_destroy(tracker), ENPU_SUCCESS);
}

/* ---------------- swap_hook: 参数校验与降级路径 ---------------- */

TEST_F(SwapModuleTest, SwapHookParamErrors)
{
    EXPECT_EQ(swap_hook_check_and_swap_in(nullptr), ENPU_SUCCESS); /* 未初始化降级: 透传 */
    EXPECT_EQ(swap_hook_free_mem(nullptr, (void *)(intptr_t)0x100), ENPU_FAIL);
    EXPECT_EQ(swap_hook_free_physical_mem(nullptr, (rtDrvMemHandle)(intptr_t)0x100), ENPU_FAIL);
    EXPECT_EQ(swap_hook_map_mem(nullptr, nullptr, 0, 0, nullptr, 0), ENPU_FAIL);
    EXPECT_EQ(swap_hook_unmap_mem(nullptr, nullptr), ENPU_FAIL);
    EXPECT_EQ(swap_hook_malloc_mem(nullptr, nullptr, 0), ENPU_FAIL);
    EXPECT_EQ(swap_hook_destroy(nullptr), ENPU_FAIL);
    EXPECT_EQ(swap_hook_create(kVnpuId, nullptr, nullptr), nullptr);
    EXPECT_EQ(swap_hook_create(-1, (void *)0x10, (void *)0x20), nullptr);

    swap_hook_t *hook = swap_hook_create(kVnpuId, (void *)0x10, (void *)0x20);
    ASSERT_NE(hook, nullptr);
    /* request == limit 时不使能 swap, 直接透传 */
    size_t savedRequest = get_mem_request_quota();
    size_t savedLimit = get_mem_limit_quota();
    set_mem_request_quota(savedLimit);
    EXPECT_EQ(swap_hook_check_and_swap_in(hook), ENPU_SUCCESS);
    set_mem_request_quota(savedRequest);
    EXPECT_EQ(swap_hook_destroy(hook), ENPU_SUCCESS);
}

/* ---------------- swap_hook: 全局初始化成功后的深路径 ---------------- */

TEST_F(SwapModuleTest, SwapHookGlobalInitSuccess)
{
    if (swap_hook_get_global() != nullptr) {
        swap_hook_global_destroy();
    }
    EXPECT_EQ(swap_hook_global_init(kVnpuId), ENPU_SUCCESS);
    EXPECT_NE(swap_hook_get_global(), nullptr);
    /* 已初始化时重复调用失败 */
    EXPECT_EQ(swap_hook_global_init(kVnpuId), ENPU_FAIL);
    EXPECT_NE(swap_hook_get_global(), nullptr);
    /* 上面的 destroy 已注销本进程, 重新注册以恢复进程级换出槽位供后续用例使用 */
    ASSERT_EQ(register_to_shm_state(get_shm_state(), kVnpuId), ENPU_SUCCESS);
    ASSERT_GE(get_registry_slot(), 0);
}

TEST_F(SwapModuleTest, SwapHookMallocAndMapMem)
{
    swap_hook_t *hook = swap_hook_get_global();
    ASSERT_NE(hook, nullptr);

    /* 虚拟内存路径: 换入(NOT_MEMCPY) + tracker 记录 + shm used 更新 */
    void *ptr = nullptr;
    uint64_t size = 4096;
    EXPECT_EQ(swap_hook_malloc_mem(hook, &ptr, size), ENPU_SUCCESS);
    EXPECT_NE(ptr, nullptr);
    EXPECT_EQ(memory_tracker_find(hook->tracker, ptr)->size, size);

    /* 物理内存路径: handle 记录 + prop 记录 */
    rtDrvMemHandle handle = (rtDrvMemHandle)(intptr_t)0x3000;
    rtDrvMemProp_t prop;
    (void)memset_s(&prop, sizeof(prop), 0, sizeof(prop));
    prop.side = ACL_MEM_LOCATION_TYPE_DEVICE;
    EXPECT_EQ(swap_hook_malloc_physical_mem(hook, handle, 8192, &prop, 0), ENPU_SUCCESS);
    EXPECT_NE(memory_tracker_find(hook->tracker, handle), nullptr);

    /* 映射路径: 已记录的 handle 命中, 生成 map 记录 */
    rtDrvMemHandle mapHandle = handle;
    EXPECT_EQ(swap_hook_map_mem(hook, (void *)(intptr_t)0x8000, 4096, 0, &mapHandle, 0x33), ENPU_SUCCESS);
    void *va = nullptr;
    EXPECT_EQ(memory_tracker_get_map_va(hook->tracker, mapHandle, &va), ENPU_SUCCESS);
    EXPECT_EQ(va, (void *)(intptr_t)0x8000);

    /* 未记录的 handle: 视为非 Device 内存, 直接成功 */
    rtDrvMemHandle unknown = (rtDrvMemHandle)(intptr_t)0x9999;
    EXPECT_EQ(swap_hook_map_mem(hook, (void *)(intptr_t)0x9000, 0, 0, &unknown, 0), ENPU_SUCCESS);

    /* 解除映射 */
    EXPECT_EQ(swap_hook_unmap_mem(hook, (void *)(intptr_t)0x8000), ENPU_SUCCESS);

    /* free: 未记录的指针 -> 记录不存在失败 */
    EXPECT_EQ(swap_hook_free_mem(hook, (void *)(intptr_t)0x7777), ENPU_FAIL);
    /* free_physical: 未记录的 handle -> 透传底层返回 */
    EXPECT_EQ(swap_hook_free_physical_mem(hook, (rtDrvMemHandle)(intptr_t)0x8888), RT_ERROR_NONE);
}

/* 进程级 hbm 记账: malloc/free 前后 processes[slot].hbm_used 与 vNPU 级 hbm_used 同步增减 */
TEST_F(SwapModuleTest, SwapHookProcessHbmAccounting)
{
    swap_hook_t *hook = swap_hook_get_global();
    ASSERT_NE(hook, nullptr);
    shm_state_t *state = get_shm_state();
    ASSERT_NE(state, nullptr);
    int slot = get_registry_slot();
    ASSERT_GE(slot, 0);
    process_entry_t *myEntry = &state->entries[kVnpuId].processes[slot];

    uint64_t procBefore = myEntry->hbm_used.load();
    uint64_t vnpuBefore = state->entries[kVnpuId].hbm_used.load();

    /* 用未占用的 handle 走物理内存记账路径, 避免与先前用例的指针记录冲突 */
    rtDrvMemHandle handle = (rtDrvMemHandle)(intptr_t)0x4400;
    rtDrvMemProp_t prop;
    (void)memset_s(&prop, sizeof(prop), 0, sizeof(prop));
    prop.side = ACL_MEM_LOCATION_TYPE_DEVICE;
    ASSERT_EQ(swap_hook_malloc_physical_mem(hook, handle, 8192, &prop, 0), ENPU_SUCCESS);
    EXPECT_EQ(myEntry->hbm_used.load(), procBefore + 8192);
    EXPECT_EQ(state->entries[kVnpuId].hbm_used.load(), vnpuBefore + 8192);

    ASSERT_EQ(swap_hook_free_physical_mem(hook, handle), ENPU_SUCCESS);
    EXPECT_EQ(myEntry->hbm_used.load(), procBefore);
    EXPECT_EQ(state->entries[kVnpuId].hbm_used.load(), vnpuBefore);
}

TEST_F(SwapModuleTest, SwapHookDeepSwapIn)
{
    swap_hook_t *hook = swap_hook_get_global();
    ASSERT_NE(hook, nullptr);
    shm_state_t *state = get_shm_state();
    ASSERT_NE(state, nullptr);

    /* 构造两条已换出记录: 虚拟型与物理型(含 handle/map 记录) */
    void *ptrA = (void *)(intptr_t)0x5100;
    rtDrvMemHandle handleA = (rtDrvMemHandle)(intptr_t)0x3100;
    ASSERT_EQ(memory_tracker_add(hook->tracker, ptrA, 2048, handleA, false), ENPU_SUCCESS);
    ASSERT_EQ(memory_tracker_mark_swapped(hook->tracker, ptrA, 0), ENPU_SUCCESS);

    void *ptrB = (void *)(intptr_t)0x5200; /* 物理型记录的 ptr 即 handle 值 */
    rtDrvMemHandle handleB = ptrB;
    ASSERT_EQ(memory_tracker_add(hook->tracker, ptrB, 1024, handleB, true), ENPU_SUCCESS);
    ASSERT_EQ(memory_tracker_mark_swapped(hook->tracker, ptrB, 2048), ENPU_SUCCESS);
    rtDrvMemProp_t prop;
    (void)memset_s(&prop, sizeof(prop), 0, sizeof(prop));
    prop.side = ACL_MEM_LOCATION_TYPE_DEVICE;
    ASSERT_EQ(memory_tracker_add_handle_record(hook->tracker, handleB, 1024, prop, 0), ENPU_SUCCESS);
    ASSERT_EQ(memory_tracker_add_map_record(hook->tracker, (void *)(intptr_t)0x8100, 1024, 0, handleB, 0),
              ENPU_SUCCESS);

    /* 单容器多进程: 标记本进程槽位已换出, 触发换入全流程.
     * swap_size 置 0 以避免 check_and_swap_out 在无 enpu-manager 的单测环境等待应答自旋 */
    int slot = get_registry_slot();
    ASSERT_GE(slot, 0);
    process_entry_t *myEntry = &state->entries[kVnpuId].processes[slot];
    myEntry->swap_size = 0;
    shm_set_process_swapped(state, kVnpuId, slot, true);
    uint64_t usedBefore = state->entries[kVnpuId].hbm_used.load();
    EXPECT_EQ(swap_hook_check_and_swap_in(hook), ENPU_SUCCESS);
    EXPECT_FALSE(memory_tracker_find(hook->tracker, ptrA)->is_swapped);
    EXPECT_FALSE(memory_tracker_find(hook->tracker, ptrB)->is_swapped);
    /* swap_size=0 时不清 swapped 标志(仅 used!=0 才恢复并清除), 保持原值 */
    EXPECT_TRUE(shm_get_process_swapped(state, kVnpuId, slot));
    EXPECT_EQ(state->entries[kVnpuId].hbm_used.load(), usedBefore);
    /* 显式复位, 隔离后续用例 */
    shm_set_process_swapped(state, kVnpuId, slot, false);

    /* 未处于换出态时直接返回 */
    EXPECT_EQ(swap_hook_check_and_swap_in(hook), ENPU_SUCCESS);
}

TEST_F(SwapModuleTest, MonitorThreadSwapOut)
{
    swap_hook_t *hook = swap_hook_get_global();
    ASSERT_NE(hook, nullptr);
    shm_state_t *state = get_shm_state();
    ASSERT_NE(state, nullptr);

    /* 全局 monitor 由 global_init 创建, 直接启动并注入换出命令 */
    swap_monitor_thread_t **monitorSlot = swap_monitor_get_thread();
    ASSERT_NE(*monitorSlot, nullptr);
    EXPECT_EQ(swap_monitor_thread_start(*monitorSlot), ENPU_SUCCESS);

    int slot = get_registry_slot();
    ASSERT_GE(slot, 0);
    void *ptrC = (void *)(intptr_t)0x5300;
    ASSERT_EQ(memory_tracker_add(hook->tracker, ptrC, 512, (rtDrvMemHandle)(intptr_t)0x3300, false), ENPU_SUCCESS);
    state->swap_cmd.completed = false;
    state->swap_out_cmd.completed = false;
    state->swap_cmd.target_vnpu_id = kVnpuId;
    state->swap_out_cmd.target_vnpu_id = kVnpuId;
    /* 单容器多进程: 命令需带序号并确保本进程未见过; refcount=1 使本进程为最后一个 ack 者.
     * 同时给本槽位落下 ack 欠账标记(真实环境由 enpu-manager 写命令时置位) */
    set_last_seen_cmd_seq(0);
    state->swap_cmd.cmd_seq.store(1);
    state->swap_cmd.refcount.store(1);
    state->entries[kVnpuId].processes[slot].ack_pending_seq.store(1);
    state->swap_cmd.action = SWAP_ACTION_OUT;

    usleep(300000); /* 等待轮询线程消费换出命令 */
    EXPECT_EQ(swap_monitor_thread_stop(*monitorSlot), ENPU_SUCCESS);
    EXPECT_TRUE(state->swap_cmd.completed);
    EXPECT_EQ(state->swap_cmd.refcount.load(), 0);
    EXPECT_EQ(state->entries[kVnpuId].processes[slot].ack_pending_seq.load(), (uint64_t)0);
    EXPECT_TRUE(shm_get_process_swapped(state, kVnpuId, slot));
    EXPECT_TRUE(memory_tracker_find(hook->tracker, ptrC)->is_swapped);
}

/* cmd_seq 去重: 序号与 last_seen 相同时监控线程跳过, 不换出/不递减 refcount/不置 completed */
TEST_F(SwapModuleTest, MonitorCmdSeqDedup)
{
    swap_hook_t *hook = swap_hook_get_global();
    ASSERT_NE(hook, nullptr);
    shm_state_t *state = get_shm_state();
    ASSERT_NE(state, nullptr);

    void *ptrD = (void *)(intptr_t)0x5400;
    ASSERT_EQ(memory_tracker_add(hook->tracker, ptrD, 256, (rtDrvMemHandle)(intptr_t)0x3400, false), ENPU_SUCCESS);

    swap_monitor_thread_t **monitorSlot = swap_monitor_get_thread();
    ASSERT_NE(*monitorSlot, nullptr);
    /* 线程启动时会把本进程 swapped 复位为 false */
    ASSERT_EQ(swap_monitor_thread_start(*monitorSlot), ENPU_SUCCESS);

    state->swap_cmd.completed = false;
    state->swap_out_cmd.completed = false;
    state->swap_cmd.target_vnpu_id = kVnpuId;
    state->swap_cmd.action = SWAP_ACTION_OUT;
    state->swap_cmd.cmd_seq.store(get_last_seen_cmd_seq()); /* 重复序号: 应被 guard 跳过 */
    state->swap_cmd.refcount.store(1);

    usleep(300000);
    EXPECT_FALSE(state->swap_cmd.completed.load());
    EXPECT_EQ(state->swap_cmd.refcount.load(), 1);
    EXPECT_FALSE(memory_tracker_find(hook->tracker, ptrD)->is_swapped);

    /* 复位共享命令, 避免影响后续用例 */
    state->swap_cmd.action = SWAP_ACTION_NONE;
    state->swap_cmd.completed = true;
    EXPECT_EQ(swap_monitor_thread_stop(*monitorSlot), ENPU_SUCCESS);
}

TEST_F(SwapModuleTest, MemoryInterceptPassthrough)
{
    /* memcpy/memset/value 族拦截: shm 已挂载, 走换入检查后透传 */
    uint8_t hostBuf[16] = {0};
    EXPECT_EQ(rtMemcpy(hostBuf, sizeof(hostBuf), hostBuf, 8, RT_MEMCPY_DEVICE_TO_HOST), RT_ERROR_NONE);
    EXPECT_EQ(rtMemcpyAsync(hostBuf, sizeof(hostBuf), hostBuf, 8, RT_MEMCPY_DEVICE_TO_HOST, nullptr), RT_ERROR_NONE);
    EXPECT_EQ(rtMemcpyAsyncEx(hostBuf, sizeof(hostBuf), hostBuf, 8, RT_MEMCPY_DEVICE_TO_HOST, nullptr, nullptr),
              RT_ERROR_NONE);
    void *dsts[1] = {hostBuf};
    void *srcs[1] = {hostBuf};
    size_t sizes[1] = {8};
    size_t attrsIdxs[1] = {0};
    size_t failIdx = 0;
    EXPECT_EQ(rtsMemcpyBatch(dsts, srcs, sizes, 1, nullptr, attrsIdxs, 0, &failIdx), RT_ERROR_NONE);
    size_t destMaxs[1] = {sizeof(hostBuf)};
    EXPECT_EQ(rtsMemcpyBatchAsync(dsts, destMaxs, srcs, sizes, 1, nullptr, attrsIdxs, 0, &failIdx, nullptr),
              RT_ERROR_NONE);
    EXPECT_EQ(rtMemcpy2d(hostBuf, 8, hostBuf, 8, 8, 1, RT_MEMCPY_DEVICE_TO_HOST), RT_ERROR_NONE);
    EXPECT_EQ(rtMemcpy2dAsync(hostBuf, 8, hostBuf, 8, 8, 1, RT_MEMCPY_DEVICE_TO_HOST, nullptr), RT_ERROR_NONE);
    const rtMemcpyKind kKindDeviceToHost = RT_MEMCPY_KIND_DEVICE_TO_HOST;
    rtMemcpyDesc_t desc = {};
    EXPECT_EQ(rtsSetMemcpyDesc(desc, kKindDeviceToHost, hostBuf, hostBuf, 8, nullptr), RT_ERROR_NONE);
    EXPECT_EQ(rtsMemcpyAsyncWithDesc(desc, kKindDeviceToHost, nullptr, nullptr), RT_ERROR_NONE);
    const void *srcList[1] = {hostBuf};
    EXPECT_EQ(rtMemcpyAsyncWithOffset((void **)hostBuf, sizeof(hostBuf), 0, srcList, 8, 0, kKindDeviceToHost, nullptr),
              RT_ERROR_NONE);
    rtPtrAttributes_t attributes = {};
    EXPECT_EQ(rtsPointerGetAttributes(hostBuf, &attributes), RT_ERROR_NONE);
    uint32_t checkResult = 0;
    void *addrs[1] = {hostBuf};
    EXPECT_EQ(rtsCheckMemType(addrs, 1, 0, &checkResult, 0), RT_ERROR_NONE);
    EXPECT_EQ(rtMemset(hostBuf, sizeof(hostBuf), 0, 8), RT_ERROR_NONE);
    EXPECT_EQ(rtMemsetAsync(hostBuf, sizeof(hostBuf), 0, 8, nullptr), RT_ERROR_NONE);
    EXPECT_EQ(rtMemPrefetchToDevice(hostBuf, 8, 0), RT_ERROR_NONE);
    char_t key[64] = {0};
    EXPECT_EQ(rtsIpcMemGetExportKey(hostBuf, 8, key, sizeof(key), 0), RT_ERROR_NONE);
    void *ipcPtr = nullptr;
    EXPECT_EQ(rtsIpcMemImportByKey(&ipcPtr, key, 0), RT_ERROR_NONE);
    EXPECT_EQ(rtsValueWrite(hostBuf, 1, 0, nullptr), RT_ERROR_NONE);
    EXPECT_EQ(rtsValueWait(hostBuf, 1, 0, nullptr), RT_ERROR_NONE);

    /* free 族: 未知指针无记录, 返回失败码 */
    EXPECT_NE(rtFree((void *)(intptr_t)0x6666), RT_ERROR_NONE);
    /* rtMemGetInfoEx/aclrtGetMemInfoImpl 参数校验 */
    size_t freeSize = 0;
    size_t totalSize = 0;
    EXPECT_EQ(rtMemGetInfoEx(RT_MEMORYINFO_HBM, nullptr, &totalSize), RT_ERROR_INVALID_VALUE);
    EXPECT_EQ(rtMemGetInfoEx(RT_MEMORYINFO_HBM, &freeSize, nullptr), RT_ERROR_INVALID_VALUE);
    EXPECT_EQ(aclrtGetMemInfoImpl(0, nullptr, &totalSize), RT_ERROR_INVALID_VALUE);
    EXPECT_EQ(aclrtGetMemInfoImpl(0, &freeSize, nullptr), RT_ERROR_INVALID_VALUE);
}

TEST_F(SwapModuleTest, GlobalInitPostAndQuotaApi)
{
    /* 重置 once 标志, 使 init_post 以已挂载的 shm 与全局 monitor 重跑(并启动 monitor) */
    post_init_flag = PTHREAD_ONCE_INIT;
    enpu_global_init_post();
    swap_monitor_thread_t **monitorSlot = swap_monitor_get_thread();
    ASSERT_NE(*monitorSlot, nullptr);

    /* check_and_swap_out: requested 为 0 时立即成功 */
    EXPECT_EQ(check_and_swap_out(0, SWAP_OUT_FROM_BORROWED), ENPU_SUCCESS);
    /* 配额读写接口 */
    set_mem_request_quota(kTestRequestMb * MB_TO_B);
    set_mem_limit_quota(kTestLimitMb * MB_TO_B);
    EXPECT_EQ(get_mem_request_quota(), (size_t)(kTestRequestMb * MB_TO_B));
    EXPECT_EQ(get_mem_limit_quota(), (size_t)(kTestLimitMb * MB_TO_B));
    EXPECT_TRUE(get_swap_enabled());
    EXPECT_EQ(get_aicore_num(), (uint32_t)DEFAULT_AICORE_NUM);
    EXPECT_EQ(get_device_id(), 0);
}

TEST_F(SwapModuleTest, DcmiWrapperRegisterAndInvalidParams)
{
    /* soc 版本分发: 950 走 dcmiv2 回调, 310/910 走 dcmi 回调 */
    EXPECT_EQ(register_callback(SOC_VERSION_ASCEND_950), ENPU_SUCCESS);
    EXPECT_EQ(register_callback(SOC_VERSION_ASCEND_310), ENPU_SUCCESS);
    EXPECT_EQ(register_callback(SOC_VERSION_ASCEND_910), ENPU_SUCCESS);

    EXPECT_EQ(enpu_dcmi_get_device_utilization_rate(0, 0, 0, nullptr), ENPU_FAIL);
    EXPECT_EQ(enpu_dcmi_get_aicore_utilization_rate(0, 0, 0, nullptr), ENPU_FAIL);
    EXPECT_EQ(enpu_dcmi_get_aicore_num(0, 0, 0, nullptr), ENPU_FAIL);
    EXPECT_EQ(enpu_dcmi_get_device_resource_info(0, 0, 0, nullptr), ENPU_FAIL);
}

TEST_F(SwapModuleTest, SwapHookGlobalDestroy)
{
    swap_hook_global_destroy();
    EXPECT_EQ(swap_hook_get_global(), nullptr);
    /* 销毁后拦截接口降级: 换入检查透传, memcpy 族直接透传成功 */
    uint8_t hostBuf[8] = {0};
    EXPECT_EQ(rtMemcpy(hostBuf, sizeof(hostBuf), hostBuf, 4, RT_MEMCPY_DEVICE_TO_HOST), RT_ERROR_NONE);
    EXPECT_NE(rtFree((void *)(intptr_t)0x6666), RT_ERROR_NONE);
    /* 再次销毁不崩溃 */
    swap_hook_global_destroy();
}
