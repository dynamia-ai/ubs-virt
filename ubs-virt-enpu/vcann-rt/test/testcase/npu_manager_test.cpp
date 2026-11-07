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
#include <mockcpp/mockcpp.hpp>

#include "common.h"
#include "config.h"
#include "core_limiter.h"
#include "dcmi_wrapper.h"
#include "log.h"
#include "mem_limiter.h"
#include "npu_manager.h"
#include "securec.h"

extern "C" {
int stub_load_config(const char *path)
{
    (void)path;
    config.phy_npu_id = 0;
    config.vnpu_id = 1;
    config.scheduling_policy = SCHED_POLICY_FIXED_SHARE;
    config.aicore_quota = 8;
    config.memory_request = 0;
    config.memory_limit = 0;
    config.memory_quota = 1024;
    strcpy_s(config.shm_id, sizeof(config.shm_id), "test_shm_id");
    return ENPU_SUCCESS;
}

int stub_enpu_dcmi_get_card_info(uint32_t phy_id, int *card_id, int *device_id, int *logic_id, uint8_t soc_version)
{
    (void)phy_id;
    (void)soc_version;
    *card_id = 0;
    *device_id = 0;
    *logic_id = 0;
    return ENPU_SUCCESS;
}

int stub_enpu_dcmi_get_device_resource_info(int logic_id, int card_id, int device_id, size_t *used)
{
    (void)logic_id;
    (void)card_id;
    (void)device_id;
    *used = 512 * MB_TO_B;
    return ENPU_SUCCESS;
}

int stub_memory_limiter_init(void)
{
    return ENPU_SUCCESS;
}

int stub_aicore_limiter_initialize(void)
{
    return ENPU_SUCCESS;
}

int stub_log_init(void)
{
    return ENPU_SUCCESS;
}
}

class NpuManagerTest : public testing::Test {
protected:
    static void SetUpTestCase()
    {
        std::cout << "npu_manager test start" << std::endl;
    }

    static void TearDownTestCase()
    {
        std::cout << "npu_manager test end" << std::endl;
    }

    void SetUp()
    {
        MOCKER(load_config).stubs().will(invoke(stub_load_config));
        MOCKER(enpu_dcmi_get_card_info).stubs().will(invoke(stub_enpu_dcmi_get_card_info));
        MOCKER(enpu_dcmi_get_device_resource_info).stubs().will(invoke(stub_enpu_dcmi_get_device_resource_info));
        MOCKER(memory_limiter_init).stubs().will(invoke(stub_memory_limiter_init));
        MOCKER(aicore_limiter_initialize).stubs().will(invoke(stub_aicore_limiter_initialize));
        MOCKER(log_init).stubs().will(invoke(stub_log_init));
    }

    void TearDown()
    {
        GlobalMockObject::verify();
        GlobalMockObject::reset();
    }
};

TEST_F(NpuManagerTest, GetMemUsedNullPtr)
{
    int ret = get_mem_used(nullptr);
    EXPECT_EQ(ret, ENPU_FAIL);
}

TEST_F(NpuManagerTest, GetMemUsedSuccess)
{
    size_t used = 0;
    int ret = get_mem_used(&used);
    EXPECT_EQ(ret, ENPU_SUCCESS);
    EXPECT_EQ(used, 512 * MB_TO_B);
}

TEST_F(NpuManagerTest, EnpuConfigInfoInitSuccess)
{
    config.phy_npu_id = 0;
    config.vnpu_id = 1;
    config.scheduling_policy = SCHED_POLICY_ELASTIC;
    config.aicore_quota = 4;
    config.memory_request = 0;
    config.memory_limit = 0;
    config.memory_quota = 512;
    strcpy_s(config.shm_id, sizeof(config.shm_id), "test_shm");

    int ret = enpu_config_info_init();
    EXPECT_EQ(ret, ENPU_SUCCESS);
    EXPECT_TRUE(is_core_limit());
    EXPECT_EQ(get_mem_limit_quota(), 512 * MB_TO_B);
    EXPECT_EQ(get_core_limit_quota(), 4);
    EXPECT_EQ(get_vnpu_id(), 1);
    EXPECT_STREQ(get_vnpu_shm_id(), "test_shm");
}

TEST_F(NpuManagerTest, EnpuConfigInfoInitBestEffortPolicy)
{
    config.phy_npu_id = 0;
    config.vnpu_id = 1;
    config.scheduling_policy = SCHED_POLICY_BEST_EFFORT;
    config.memory_request = 0;
    config.memory_limit = 0;
    config.memory_quota = 1024;
    strcpy_s(config.shm_id, sizeof(config.shm_id), "best_effort_shm");

    int ret = enpu_config_info_init();
    EXPECT_EQ(ret, ENPU_SUCCESS);
    EXPECT_FALSE(is_core_limit());
    EXPECT_EQ(get_mem_limit_quota(), 1024 * MB_TO_B);
    EXPECT_EQ(get_vnpu_id(), 1);
    EXPECT_STREQ(get_vnpu_shm_id(), "best_effort_shm");
}

TEST_F(NpuManagerTest, EnpuConfigInfoInitInvalidPolicy)
{
    config.scheduling_policy = (schedule_policy_t)999;
    int ret = enpu_config_info_init();
    EXPECT_EQ(ret, ENPU_FAIL);
}

TEST_F(NpuManagerTest, EnpuLoadConfigSuccess)
{
    int ret = enpu_load_config();
    EXPECT_EQ(ret, ENPU_SUCCESS);
}

TEST_F(NpuManagerTest, EnpuDeviceInitSuccess)
{
    int ret = enpu_device_init();
    EXPECT_EQ(ret, ENPU_SUCCESS);
}

TEST_F(NpuManagerTest, EnpuGlobalInitSuccess)
{
    enpu_global_init(); // should call once
    enpu_global_init(); // should not call again
    // No crash or error expected
}

/* cmd_seq 去重依赖的进程级序号存取接口 */
TEST_F(NpuManagerTest, CmdSeqAccessorRoundTrip)
{
    uint64_t saved = get_last_seen_cmd_seq();
    set_last_seen_cmd_seq(12345);
    EXPECT_EQ(get_last_seen_cmd_seq(), (uint64_t)12345);
    set_last_seen_cmd_seq(saved);
}

/* 注册后写回本进程槽位; 注销后槽位复位为 -1 */
TEST_F(NpuManagerTest, RegisterToShmStateAssignsRegistrySlot)
{
    shm_state_t state = {0};
    state.next_vcann_rt_id.store(1);

    ASSERT_EQ(register_to_shm_state(&state, 0), ENPU_SUCCESS);
    int slot = get_registry_slot();
    EXPECT_GE(slot, 0);
    EXPECT_LT(slot, MAX_PROCESSES_PER_VNPU);
    EXPECT_TRUE(state.entries[0].processes[slot].active.load());
    EXPECT_GE(state.entries[0].processes[slot].vcann_rt_id, 1);

    unregister_from_shm_state(&state, 0, slot);
    EXPECT_EQ(get_registry_slot(), -1);
    EXPECT_FALSE(state.entries[0].processes[slot].active.load());
}
