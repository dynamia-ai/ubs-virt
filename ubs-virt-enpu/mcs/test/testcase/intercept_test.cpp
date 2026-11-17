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

#include <fcntl.h>
#include <gtest/gtest.h>
#include <unistd.h>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>

#include "intercept_api.h"
#include "mcs_config.h"
#include "mcs_device.h"
#include "mcs_log.h"
#include "mcs_rtapi.h"
#include "mcs_sched.h"
#include "runtime_stub.h"

namespace {
std::string ConfigContent(const std::string &shmId, int quota = 50)
{
    return "physical-npu-id=0\n"
           "virtual-npu-id=3\n"
           "aicore-quota=" +
           std::to_string(quota) +
           "\n"
           "memory-quota=1024\n"
           "vnpu_priority=0\n"
           "shm-id=" +
           shmId + "\n";
}

std::string WriteConfig(const std::string &content)
{
    char path[] = "/tmp/mcs-intercept-XXXXXX";
    int fd = mkstemp(path);
    EXPECT_GE(fd, 0);
    if (fd < 0) {
        return "";
    }
    EXPECT_EQ(write(fd, content.data(), content.size()), static_cast<ssize_t>(content.size()));
    close(fd);
    return path;
}

void UseTempLogDir(const char *suffix)
{
    int ret =
        snprintf(g_log_config.log_dir, sizeof(g_log_config.log_dir), "/tmp/mcs-intercept-%s-%d/", suffix, getpid());
    ASSERT_GE(ret, 0);
    ASSERT_LT(static_cast<size_t>(ret), sizeof(g_log_config.log_dir));
    g_log_config.log_path[0] = '\0';
    g_log_config.log_file_path[0] = '\0';
    g_log_config.compress_disabled = true;
}

class EarlyInterceptTest : public testing::Test {
protected:
    void SetUp() override
    {
        reset_runtime_stub();
        UseTempLogDir("early");
    }

    void TearDown() override
    {
        log_shutdown();
        unsetenv("MCS_CONFIG");
        if (!path_.empty()) {
            unlink(path_.c_str());
        }
        reset_runtime_stub();
    }

    std::string path_;
};

std::string g_init_config_path;
std::string g_init_shm_name;
mcs_shared_state_t *g_init_state = nullptr;

class InterceptTest : public testing::Test {
protected:
    static void SetUpTestSuite()
    {
        reset_runtime_stub();
        g_init_shm_name = "/mcs-intercept-" + std::to_string(getpid());
        g_init_config_path = WriteConfig(ConfigContent(g_init_shm_name));
        ASSERT_FALSE(g_init_config_path.empty());
        setenv("MCS_CONFIG", g_init_config_path.c_str(), 1);
        UseTempLogDir("initialized");
        ASSERT_EQ(rtSetDevice(3), 0);
        ASSERT_EQ(mcs_get_limited_aicube(), 16);
        ASSERT_EQ(mcs_get_limited_aivector(), 8);
    }

    static void TearDownTestSuite()
    {
        log_shutdown();
        mcs_shm_cleanup(g_init_shm_name.c_str(), g_init_state);
        unlink(g_init_config_path.c_str());
        unsetenv("MCS_CONFIG");
    }

    void SetUp() override
    {
        reset_runtime_stub();
    }
};
} // namespace

TEST_F(EarlyInterceptTest, LazyCoreQueryFallsBackWhenConfigIsMissing)
{
    setenv("MCS_CONFIG", "/tmp/mcs-no-such-config", 1);
    uint32_t count = 0;
    EXPECT_EQ(rtGetAiCoreCount(&count), 0);
    EXPECT_EQ(count, 32U);

    g_stub_core_count_return = 42;
    count = 0;
    EXPECT_EQ(rtGetAiCoreCount(&count), 42);
    EXPECT_EQ(count, 0U);
}

TEST_F(EarlyInterceptTest, LazyCoreQueryUsesQuotaBeforeInitialization)
{
    path_ = WriteConfig(ConfigContent("/mcs-lazy-unused"));
    setenv("MCS_CONFIG", path_.c_str(), 1);
    uint32_t count = 0;
    EXPECT_EQ(rtGetAiCoreCount(&count), 0);
    EXPECT_EQ(count, 16U);
}

TEST_F(EarlyInterceptTest, SetDeviceRejectsMissingConfig)
{
    setenv("MCS_CONFIG", "/tmp/mcs-no-such-config", 1);
    EXPECT_EQ(rtSetDevice(0), -1);
}

TEST_F(EarlyInterceptTest, SetDeviceRejectsUnknownPhysicalCoreCount)
{
    path_ = WriteConfig(ConfigContent("/mcs-no-core-unused"));
    setenv("MCS_CONFIG", path_.c_str(), 1);
    g_stub_physical_aicube = 0;
    EXPECT_EQ(rtSetDevice(0), -1);
}

TEST_F(EarlyInterceptTest, SetDeviceRejectsSharedMemoryFailure)
{
    path_ = WriteConfig(ConfigContent("invalid/shm-name"));
    setenv("MCS_CONFIG", path_.c_str(), 1);
    EXPECT_EQ(rtSetDevice(0), -1);
}

TEST_F(EarlyInterceptTest, ResourceQueryPassesThroughBeforeInitialization)
{
    uint32_t value = 0;
    EXPECT_EQ(rtsGetResInCurrentThread(RT_DEV_RES_CUBE_CORE, &value), 88);
    EXPECT_EQ(value, 99U);
    EXPECT_EQ(rtsGetResInCurrentThread(RT_DEV_RES_CUBE_CORE, nullptr), -1);
}

TEST_F(InterceptTest, SetDeviceIsIdempotentAndReturnsRuntimeResult)
{
    int calls = g_stub_set_device_calls;
    EXPECT_EQ(rtSetDevice(3), 0);
    EXPECT_EQ(g_stub_set_device_calls, calls + 1);
    g_stub_set_device_return = 12;
    EXPECT_EQ(rtSetDevice(3), 12);
}

TEST_F(InterceptTest, DeviceInfoReturnsLimitsOrPassesThrough)
{
    int64_t value = 0;
    EXPECT_EQ(rtGetDeviceInfo(0, MCS_MODULE_TYPE_AICORE, MCS_INFO_TYPE_CORE_NUM, &value), 0);
    EXPECT_EQ(value, 16);
    EXPECT_EQ(rtGetDeviceInfo(0, MCS_MODULE_TYPE_AICORE, MCS_INFO_TYPE_CUBE_NUM, &value), 0);
    EXPECT_EQ(value, 16);
    EXPECT_EQ(rtGetDeviceInfo(0, MCS_MODULE_TYPE_VECTOR_CORE, MCS_INFO_TYPE_CORE_NUM, &value), 0);
    EXPECT_EQ(value, 8);

    g_stub_device_info_return = 19;
    EXPECT_EQ(rtGetDeviceInfo(0, 100, 100, &value), 19);
    EXPECT_EQ(value, 32);
    EXPECT_EQ(rtGetDeviceInfo(0, 100, 100, nullptr), 19);
}

TEST_F(InterceptTest, CoreCountAndResourceQueriesReturnLimits)
{
    uint32_t count = 0;
    EXPECT_EQ(rtGetAiCoreCount(&count), 0);
    EXPECT_EQ(count, 16U);
    EXPECT_EQ(rtGetAiCoreCount(nullptr), -1);

    uint32_t value = 0;
    EXPECT_EQ(rtsGetResInCurrentThread(RT_DEV_RES_CUBE_CORE, &value), 0);
    EXPECT_EQ(value, 16U);
    EXPECT_EQ(rtsGetResInCurrentThread(RT_DEV_RES_VECTOR_CORE, &value), 0);
    EXPECT_EQ(value, 8U);
    EXPECT_EQ(rtsGetResInCurrentThread(RT_DEV_RES_TYPE_MAX, &value), 88);
    EXPECT_EQ(value, 99U);
    EXPECT_EQ(rtsGetResInCurrentThread(-1, &value), 88);
    EXPECT_EQ(rtsGetResInCurrentThread(0, nullptr), -1);
}

TEST_F(InterceptTest, StubBasedKernelLaunchesPassThrough)
{
    EXPECT_EQ(rtKernelLaunch(nullptr, 7, nullptr, 0, nullptr, nullptr), 77);
    EXPECT_EQ(rtKernelLaunchWithFlag(nullptr, 7, nullptr, nullptr, nullptr, 1), 77);
    EXPECT_EQ(rtKernelLaunchWithFlagV2(nullptr, 7, nullptr, nullptr, nullptr, 1, nullptr), 77);
    EXPECT_EQ(rtVectorCoreKernelLaunch(nullptr, 7, nullptr, nullptr, nullptr, 1, nullptr), 77);
}

TEST_F(InterceptTest, HandleBasedKernelLaunchesPassThrough)
{
    EXPECT_EQ(rtKernelLaunchWithHandle(nullptr, 1, 7, nullptr, nullptr, nullptr, nullptr), 77);
    EXPECT_EQ(rtKernelLaunchWithHandleV2(nullptr, 1, 7, nullptr, nullptr, nullptr, nullptr), 77);
    EXPECT_EQ(rtVectorCoreKernelLaunchWithHandle(nullptr, 1, 7, nullptr, nullptr, nullptr, nullptr), 77);
    EXPECT_EQ(rtLaunchKernelByFuncHandle(nullptr, 7, nullptr, nullptr), 77);
    EXPECT_EQ(rtLaunchKernelByFuncHandleV2(nullptr, 7, nullptr, nullptr, nullptr), 77);
    EXPECT_EQ(rtLaunchKernelByFuncHandleV3(nullptr, 7, nullptr, nullptr, nullptr), 77);
}

TEST_F(InterceptTest, FrameworkAndRtsLaunchesPassThrough)
{
    EXPECT_EQ(rtKernelLaunchEx(nullptr, 0, 0, nullptr), 77);
    EXPECT_EQ(rtKernelLaunchFwk("op", nullptr, 0, 0, nullptr), 77);
    EXPECT_EQ(rtKernelLaunchFwk(nullptr, nullptr, 0, 0, nullptr), 77);
    EXPECT_EQ(rtsLaunchKernelWithDevArgs(nullptr, 7, nullptr, nullptr, nullptr, 0, nullptr), 77);
    EXPECT_EQ(rtsLaunchKernelWithConfig(nullptr, 7, nullptr, nullptr, nullptr, nullptr), 77);
    EXPECT_EQ(rtsLaunchKernelWithHostArgs(nullptr, 7, nullptr, nullptr, nullptr, 0, nullptr, 0), 77);
    EXPECT_EQ(rtLaunchKernelWithArgsArray(nullptr, 7, nullptr, nullptr, nullptr), 77);
}

TEST_F(InterceptTest, LaunchReturnsErrorWhenRealFunctionIsUnavailable)
{
    auto saved = real_rtKernelLaunch;
    real_rtKernelLaunch = nullptr;
    EXPECT_EQ(rtKernelLaunch(nullptr, 7, nullptr, 0, nullptr, nullptr), -1);
    real_rtKernelLaunch = saved;
}
