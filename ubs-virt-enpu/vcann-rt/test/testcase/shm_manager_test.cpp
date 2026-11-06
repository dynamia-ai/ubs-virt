/*
* Copyright (c) Huawei Technologies Co., Ltd. 2026-2026. All rights reserved.
* ubs-virt-enpu is licensed under Mulan PSL v2.
* You can use this software according to the terms and conditions of the Mulan PSL v2.
* You may obtain a copy of Mulan PSL v2 at:
*          http://license.coscl.org.cn/MulanPSL2
* THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND,
* EITHER EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT,
* MERCHANTABILITY OR FIT FOR A SPECIFIC PURPOSE.
* See the Mulan PSL v2 for more details.
*/

#include "shm_manager.h"
#include <fcntl.h>
#include <gtest/gtest.h>
#include <sys/stat.h>
#include <unistd.h>
#include <atomic>
#include "core_limiter.h"
#include "log.h"
#include "npu_manager.h"

class ShmManagerTest : public testing::Test {
protected:
    static void SetUpTestCase()
    {
        std::cout << "ShmManager test start" << std::endl;
    }

    static void TearDownTestCase()
    {
        std::cout << "ShmManager test end" << std::endl;
    }

    void SetUp()
    {
        (void)sprintf_s(g_log_config.log_dir, sizeof(g_log_config.log_dir), "%s", "../__build/log/enpu/");
    }

    void TearDown() {}
};

TEST_F(ShmManagerTest, ShmGetEntryNullState)
{
    vnpu_shm_entry_t *entry = shm_get_entry(NULL, 5);
    EXPECT_EQ(entry, nullptr);
}

TEST_F(ShmManagerTest, ShmGetEntryInvalidVnpuId)
{
    shm_state_t state = {0};

    vnpu_shm_entry_t *entry = shm_get_entry(&state, -1);
    EXPECT_EQ(entry, nullptr);

    entry = shm_get_entry(&state, MAX_VNPU_PER_DIE);
    EXPECT_EQ(entry, nullptr);
}

TEST_F(ShmManagerTest, ShmGetEntryNotInBitmap)
{
    shm_state_t state = {0};

    vnpu_shm_entry_t *entry = shm_get_entry(&state, 5);
    EXPECT_EQ(entry, nullptr);
}

TEST_F(ShmManagerTest, ShmGetEntryFound)
{
    shm_state_t state = {0};
    vnpu_bitmap_set(state.vnpu_bitmap, 5);
    state.entries[5].vnpu_id = 5;
    state.entries[5].hbm_request = 8192;
    state.entries[5].hbm_limit = 32768;

    vnpu_shm_entry_t *entry = shm_get_entry(&state, 5);
    EXPECT_NE(entry, nullptr);
    EXPECT_EQ(entry->vnpu_id, 5);
    EXPECT_EQ(entry->hbm_request, 8192);
    EXPECT_EQ(entry->hbm_limit, 32768);
    EXPECT_EQ(entry, &state.entries[5]);
}

TEST_F(ShmManagerTest, ShmUpdateUsedNullState)
{
    int rc = shm_update_used(NULL, 5, 1024);
    EXPECT_EQ(rc, ENPU_FAIL);
}

TEST_F(ShmManagerTest, ShmUpdateUsedEntryNotInBitmap)
{
    shm_state_t state = {0};

    int rc = shm_update_used(&state, 5, 1024);
    EXPECT_EQ(rc, ENPU_FAIL);
}

TEST_F(ShmManagerTest, ShmUpdateUsedSuccess)
{
    shm_state_t state = {0};
    vnpu_bitmap_set(state.vnpu_bitmap, 5);

    int rc = shm_update_used(&state, 5, 4096);
    EXPECT_EQ(rc, ENPU_SUCCESS);
    EXPECT_EQ(state.entries[5].hbm_used, 4096);
}

TEST_F(ShmManagerTest, ShmStateStructSize)
{
    EXPECT_GT(sizeof(shm_state_t), sizeof(vnpu_shm_entry_t) * MAX_VNPU_PER_DIE);
}

TEST_F(ShmManagerTest, ShmGetDynamicFreeNullState)
{
    uint64_t freeSize = shm_get_dynamic_free(NULL);
    EXPECT_EQ(freeSize, 0);
}

TEST_F(ShmManagerTest, ShmGetDynamicFreeEmpty)
{
    shm_state_t state = {0};
    state.hbm_total = 65536;

    uint64_t freeSize = shm_get_dynamic_free(&state);
    EXPECT_EQ(freeSize, 65536);
}

TEST_F(ShmManagerTest, ShmGetDynamicFreeSingleEntry)
{
    shm_state_t state = {0};
    state.hbm_total = 65536 + MB_TO_B; /* 容纳 1MB 配额后剩 65536 */
    vnpu_bitmap_set(state.vnpu_bitmap, 5);
    state.entries[5].hbm_request = (uint64_t)1; /* 1MB */
    state.entries[5].hbm_used = (uint64_t)4096; /* used < request, 不额外扣减 */

    uint64_t freeSize = shm_get_dynamic_free(&state);
    EXPECT_EQ(freeSize, (uint64_t)65536);
}

TEST_F(ShmManagerTest, ShmGetDynamicFreeMultipleEntries)
{
    shm_state_t state = {0};
    state.hbm_total = 65536 + 3 * MB_TO_B;
    vnpu_bitmap_set(state.vnpu_bitmap, 5);
    state.entries[5].hbm_request = (uint64_t)1; /* 1MB */
    state.entries[5].hbm_used = (uint64_t)8192;
    vnpu_bitmap_set(state.vnpu_bitmap, 10);
    state.entries[10].hbm_request = (uint64_t)2; /* 2MB */
    state.entries[10].hbm_used = (uint64_t)4096;

    uint64_t freeSize = shm_get_dynamic_free(&state);
    EXPECT_EQ(freeSize, 65536);
}

TEST_F(ShmManagerTest, ShmGetDynamicFreeOverUsedDeducted)
{
    shm_state_t state = {0};
    state.hbm_total = 65536 + MB_TO_B + 4096;
    vnpu_bitmap_set(state.vnpu_bitmap, 5);
    state.entries[5].hbm_request = (uint64_t)1; /* 1MB */
    state.entries[5].hbm_used = MB_TO_B + 4096; /* 超用 4096 字节 */

    uint64_t freeSize = shm_get_dynamic_free(&state);
    EXPECT_EQ(freeSize, 65536);
}

TEST_F(ShmManagerTest, ShmGetDynamicFreeExhausted)
{
    shm_state_t state = {0};
    state.hbm_total = 4096;
    vnpu_bitmap_set(state.vnpu_bitmap, 5);
    state.entries[5].hbm_request = (uint64_t)1; /* 1MB, 已超出 total */

    uint64_t freeSize = shm_get_dynamic_free(&state);
    EXPECT_EQ(freeSize, 0);
}

TEST_F(ShmManagerTest, ShmGetHbmRequestFreeNullState)
{
    uint64_t freeSize = shm_get_hbm_request_free(NULL);
    EXPECT_EQ(freeSize, 0);
}

/* 多进程注册到同一 vNPU: 各自占用独立槽位; 注销单个进程不影响其它进程 */
TEST_F(ShmManagerTest, ProcessRegisterMultipleSlotsAndUnregisterOne)
{
    shm_state_t state = {0};
    vnpu_bitmap_set(state.vnpu_bitmap, 5);
    state.next_vcann_rt_id.store(1);

    ASSERT_EQ(register_to_shm_state(&state, 5), ENPU_SUCCESS);
    int slot0 = get_registry_slot();
    ASSERT_GE(slot0, 0);
    ASSERT_EQ(register_to_shm_state(&state, 5), ENPU_SUCCESS);
    int slot1 = get_registry_slot();
    ASSERT_GE(slot1, 0);
    EXPECT_NE(slot0, slot1);
    EXPECT_TRUE(state.entries[5].processes[slot0].active.load());
    EXPECT_TRUE(state.entries[5].processes[slot1].active.load());
    EXPECT_NE(state.entries[5].processes[slot0].vcann_rt_id, state.entries[5].processes[slot1].vcann_rt_id);

    /* 换出状态按进程槽位独立维护, vNPU 级聚合要求全部活跃进程均已换出 */
    EXPECT_FALSE(shm_get_vnpu_all_swapped(&state, 5));
    shm_set_process_swapped(&state, 5, slot0, true);
    EXPECT_TRUE(shm_get_process_swapped(&state, 5, slot0));
    EXPECT_FALSE(shm_get_vnpu_all_swapped(&state, 5));
    shm_set_process_swapped(&state, 5, slot1, true);
    EXPECT_TRUE(shm_get_vnpu_all_swapped(&state, 5));

    int32_t id1 = state.entries[5].processes[slot1].vcann_rt_id;
    unregister_from_shm_state(&state, 5, slot0);
    EXPECT_FALSE(state.entries[5].processes[slot0].active.load());
    EXPECT_TRUE(state.entries[5].processes[slot1].active.load());
    EXPECT_TRUE(shm_get_process_swapped(&state, 5, slot1));
    EXPECT_EQ(state.entries[5].processes[slot1].vcann_rt_id, id1);

    unregister_from_shm_state(&state, 5, slot1);
    EXPECT_TRUE(shm_get_vnpu_all_swapped(&state, 5)); /* 无活跃进程视为已换出 */
}

/* 进程心跳: 注册时写入时间戳; 心跳超过超时阈值时可被判定为死进程, 槽位回收后可复用 */
TEST_F(ShmManagerTest, ProcessHeartbeatTimeoutAndSlotReclaim)
{
    shm_state_t state = {0};
    vnpu_bitmap_set(state.vnpu_bitmap, 6);
    state.next_vcann_rt_id.store(1);

    ASSERT_EQ(register_to_shm_state(&state, 6), ENPU_SUCCESS);
    int slot = get_registry_slot();
    ASSERT_GE(slot, 0);

    process_entry_t *pe = &state.entries[6].processes[slot];
    uint64_t hb = pe->last_heartbeat_ns.load();
    EXPECT_GT(hb, (uint64_t)0);

    const uint64_t timeoutNs = 5ULL * NS_PER_S; /* 与 enpu-manager PROCESS_ALIVE_TIMEOUT_NS 一致 */
    uint64_t now = ns_now();
    ASSERT_GT(now, timeoutNs + NS_PER_S);
    pe->last_heartbeat_ns.store(now - timeoutNs - NS_PER_S);
    hb = pe->last_heartbeat_ns.load();
    EXPECT_GT(now - hb, timeoutNs);

    /* 死进程回收: 余额清零、槽位释放后可被新进程复用 */
    pe->hbm_used.store(4096);
    state.entries[6].hbm_used.store(4096);
    unregister_from_shm_state(&state, 6, slot);
    EXPECT_FALSE(pe->active.load());
    EXPECT_EQ(state.entries[6].hbm_used.load(), (uint64_t)0);

    ASSERT_EQ(register_to_shm_state(&state, 6), ENPU_SUCCESS);
    EXPECT_EQ(get_registry_slot(), slot);
    unregister_from_shm_state(&state, 6, get_registry_slot());
}
