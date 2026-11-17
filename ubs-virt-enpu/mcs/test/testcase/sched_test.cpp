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
#include <time.h>
#include <cstdlib>
#include <cstring>
#include <string>

#include "mcs_sched.h"

namespace {
uint64_t NowNs()
{
    timespec ts{};
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return static_cast<uint64_t>(ts.tv_sec) * 1000000000ULL + static_cast<uint64_t>(ts.tv_nsec);
}

class SchedTest : public testing::Test {
protected:
    void SetUp() override
    {
        memset(&state_, 0, sizeof(state_));
    }

    mcs_shared_state_t state_{};
};
} // namespace

TEST_F(SchedTest, InvalidArgumentsAreSafeNoOps)
{
    mcs_mark_active(nullptr, 0);
    mcs_mark_active(&state_, -1);
    mcs_mark_active(&state_, MCS_MAX_VNPU);
    EXPECT_EQ(mcs_has_high_priority_active(nullptr, 0), 0);
    EXPECT_EQ(mcs_has_high_priority_active(&state_, -1), 0);
    EXPECT_EQ(mcs_has_high_priority_active(&state_, MCS_MAX_VNPU), 0);
    mcs_wait_for_idle(nullptr, 0);
    mcs_before_launch(nullptr, 0, 0);
    mcs_before_launch(&state_, -1, 0);
    mcs_before_launch(&state_, MCS_MAX_VNPU, 1);
    EXPECT_EQ(state_.high_launch_count, 0U);
}

TEST_F(SchedTest, MarksRecentActivity)
{
    uint64_t before = NowNs();
    mcs_mark_active(&state_, 3);
    EXPECT_GE(state_.last_kernel_time_ns[3], before);
}

TEST_F(SchedTest, DetectsOnlyRecentHighPriorityPeers)
{
    state_.vnpu_priority[0] = 0;
    state_.vnpu_priority[1] = 1;
    state_.vnpu_priority[2] = 0;
    state_.last_kernel_time_ns[1] = NowNs();
    state_.last_kernel_time_ns[2] = NowNs();
    EXPECT_EQ(mcs_has_high_priority_active(&state_, 0), 1);
    EXPECT_EQ(mcs_has_high_priority_active(&state_, 1), 0);
    EXPECT_EQ(mcs_has_high_priority_active(&state_, 2), 1);

    state_.last_kernel_time_ns[1] = 0;
    EXPECT_EQ(mcs_has_high_priority_active(&state_, 0), 0);
}

TEST_F(SchedTest, ExpiredAndFutureOutsideWindowAreIdle)
{
    state_.vnpu_priority[1] = 1;
    state_.last_kernel_time_ns[1] = NowNs() - MCS_NO_TASK_TIMEOUT_NS - 1000000ULL;
    EXPECT_EQ(mcs_has_high_priority_active(&state_, 0), 0);

    state_.last_kernel_time_ns[1] = NowNs() + MCS_NO_TASK_TIMEOUT_NS + 1000000ULL;
    EXPECT_EQ(mcs_has_high_priority_active(&state_, 0), 0);
}

TEST_F(SchedTest, WaitReturnsWhenHighPriorityIsIdle)
{
    state_.vnpu_priority[1] = 1;
    state_.last_kernel_time_ns[1] = NowNs() - MCS_NO_TASK_TIMEOUT_NS - 1;
    mcs_wait_for_idle(&state_, 0);
    SUCCEED();
}

TEST_F(SchedTest, HighPriorityLaunchIncrementsBurstWithoutWaiting)
{
    state_.vnpu_priority[0] = 1;
    state_.vnpu_priority[1] = 1;
    state_.last_kernel_time_ns[1] = NowNs();
    mcs_before_launch(&state_, 0, 1);
    mcs_before_launch(&state_, 0, 1);
    EXPECT_EQ(state_.high_launch_count, 2U);
    EXPECT_GT(state_.last_kernel_time_ns[0], 0U);
}

TEST_F(SchedTest, NormalLaunchResetsBurstWhenNoHighPriorityIsActive)
{
    state_.high_launch_count = 9;
    mcs_before_launch(&state_, 0, 0);
    EXPECT_EQ(state_.high_launch_count, 0U);
    EXPECT_GT(state_.last_kernel_time_ns[0], 0U);
}

TEST_F(SchedTest, BurstLimitPreventsNormalPriorityStarvation)
{
    setenv("MCS_STARVATION_LIMIT", "1", 1);
    state_.vnpu_priority[1] = 1;
    state_.last_kernel_time_ns[1] = NowNs();
    state_.high_launch_count = 1;
    mcs_before_launch(&state_, 0, 0);
    EXPECT_EQ(state_.high_launch_count, 0U);
    EXPECT_GT(state_.last_kernel_time_ns[0], 0U);
    unsetenv("MCS_STARVATION_LIMIT");
}

TEST(SharedMemoryTest, InitializesMapsAndCleansUpState)
{
    EXPECT_EQ(mcs_shm_init(nullptr, nullptr), -1);
    std::string name = "/mcs-ut-" + std::to_string(getpid());
    mcs_shared_state_t *state = nullptr;
    ASSERT_EQ(mcs_shm_init(name.c_str(), &state), 0);
    ASSERT_NE(state, nullptr);
    EXPECT_EQ(state->magic_number, MCS_MAGIC_INITIALIZED);
    EXPECT_EQ(state->high_launch_count, 0U);

    mcs_shared_state_t *second = nullptr;
    ASSERT_EQ(mcs_shm_init(name.c_str(), &second), 0);
    EXPECT_EQ(second->magic_number, MCS_MAGIC_INITIALIZED);
    mcs_shm_cleanup(nullptr, nullptr);
    mcs_shm_cleanup(name.c_str(), state);
    mcs_shm_cleanup(nullptr, second);
}
