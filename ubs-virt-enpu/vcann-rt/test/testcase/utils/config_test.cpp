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

#include "config.h"
#include <acl/acl.h>
#include <gtest/gtest.h>
#include <runtime/rt.h>
#include <sys/file.h>
#include <sys/mman.h>
#include <mockcpp/mockcpp.hpp>
#include "log.h"
#include "mem_limiter.h"
#include "npu_manager.h"
#include "runtime_stub.h"
#include "securec.h"

class ConfigTest : public testing::Test {
protected:
    static void SetUpTestCase()
    {
        std::cout << "Config test start" << std::endl;
    }

    static void TearDownTestCase()
    {
        std::cout << "Config test end" << std::endl;
    }

    void SetUp()
    {
        (void)sprintf_s(g_log_config.log_dir, sizeof(g_log_config.log_dir), "%s", "../__build/log/enpu/");
        fd_ = open(stub_lock_path(), O_CREAT | O_RDONLY, 0755); // ut中的memctl.lock文件,设置为755权限
        MOCKER(lock_path).stubs().will(invoke(stub_lock_path));
        MOCKER(enpu_load_config).stubs().will(invoke(stub_enpu_load_config));
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

TEST_F(ConfigTest, LoadConfigTest)
{
    int rc = load_config(MOCK_NPU_CONFIG_PATH);
    EXPECT_EQ(rc, RT_ERROR_NONE);
    rc = load_config(nullptr);
    EXPECT_EQ(rc, ENPU_FAIL);
}

TEST_F(ConfigTest, CheckInt32Test)
{
    int rc = check_int32(-1, nullptr);
    EXPECT_EQ(rc, ENPU_FAIL);
}

TEST_F(ConfigTest, CheckStrTest)
{
    std::string str = "";
    int rc = check_str(str.c_str(), nullptr);
    EXPECT_EQ(rc, ENPU_FAIL);
}

TEST_F(ConfigTest, CheckShmIdRejectsInvalid)
{
    EXPECT_EQ(check_shm_id("", OPTION_SHM_ID), ENPU_FAIL);
    EXPECT_EQ(check_shm_id("/tmp/shm", OPTION_SHM_ID), ENPU_FAIL);
    EXPECT_EQ(check_shm_id("shm/id", OPTION_SHM_ID), ENPU_FAIL);
    EXPECT_EQ(check_shm_id("..", OPTION_SHM_ID), ENPU_FAIL);
    EXPECT_EQ(check_shm_id("../shm", OPTION_SHM_ID), ENPU_FAIL);
}

TEST_F(ConfigTest, CheckShmIdAcceptsValid)
{
    EXPECT_EQ(check_shm_id("AAAAAAAA-BBBBBBBB", OPTION_SHM_ID), ENPU_SUCCESS);
    EXPECT_EQ(check_shm_id("shm.id.with.dots", OPTION_SHM_ID), ENPU_SUCCESS);
}

// A value whose length equals ret_len must be rejected (no room for the NUL).
TEST_F(ConfigTest, LoadStrBoundaryRejected)
{
    constexpr size_t kBufLen = 8;
    char buf[kBufLen] = {0};
    EXPECT_EQ(load_str(OPTION_SHM_ID, "1234567", buf, kBufLen), ENPU_SUCCESS);
    EXPECT_EQ(load_str(OPTION_SHM_ID, "12345678", buf, kBufLen), ENPU_FAIL);
}

// check_shm_id is wired into load_config: a path-traversal shm-id fails the load.
TEST_F(ConfigTest, LoadConfigRejectsInvalidShmId)
{
    const char *path = "../__build/bad_shm_id.config";
    const char *content = "physical-npu-id=0\n"
                          "virtual-npu-id=0\n"
                          "aicore-quota=50\n"
                          "memory-quota=32768\n"
                          "scheduling-policy=1\n"
                          "shm-id=../evil\n";
    FILE *f = fopen(path, "w");
    ASSERT_NE(f, nullptr);
    ASSERT_EQ(fputs(content, f) >= 0, true);
    ASSERT_EQ(fclose(f), 0);

    EXPECT_EQ(load_config(path), ENPU_FAIL);
    remove(path);

    // Restore the global config so later fixtures see a valid one.
    ASSERT_EQ(load_config(MOCK_NPU_CONFIG_PATH), ENPU_SUCCESS);
}

// memory_request and memory_limit must appear together: one without the other fails the load.
TEST_F(ConfigTest, LoadConfigRejectsRequestLimitHalfConfigured)
{
    const char *reqOnly = "physical-npu-id=0\n"
                          "virtual-npu-id=0\n"
                          "aicore-quota=50\n"
                          "memory-quota=32768\n"
                          "memory-request=10240\n"
                          "scheduling-policy=2\n"
                          "shm-id=14422CC3-2040D918-27A73226-80B40A0A-BB100003\n";
    const char *limitOnly = "physical-npu-id=0\n"
                            "virtual-npu-id=0\n"
                            "aicore-quota=50\n"
                            "memory-quota=32768\n"
                            "memory-limit=30720\n"
                            "scheduling-policy=2\n"
                            "shm-id=14422CC3-2040D918-27A73226-80B40A0A-BB100003\n";
    for (const char *content : {reqOnly, limitOnly}) {
        const char *path = "../__build/half_memory.config";
        FILE *f = fopen(path, "w");
        ASSERT_NE(f, nullptr);
        ASSERT_EQ(fputs(content, f) >= 0, true);
        ASSERT_EQ(fclose(f), 0);
        EXPECT_EQ(load_config(path), ENPU_FAIL);
        remove(path);
    }

    // Restore the global config so later fixtures see a valid one.
    ASSERT_EQ(load_config(MOCK_NPU_CONFIG_PATH), ENPU_SUCCESS);
}

// A quota-only config fills memory_request and memory_limit from memory_quota.
TEST_F(ConfigTest, LoadConfigQuotaOnlyCompletesRequestLimit)
{
    const char *path = "../__build/quota_only.config";
    const char *content = "physical-npu-id=0\n"
                          "virtual-npu-id=0\n"
                          "aicore-quota=50\n"
                          "memory-quota=32768\n"
                          "scheduling-policy=1\n"
                          "shm-id=14422CC3-2040D918-27A73226-80B40A0A-BB100003\n";
    FILE *f = fopen(path, "w");
    ASSERT_NE(f, nullptr);
    ASSERT_EQ(fputs(content, f) >= 0, true);
    ASSERT_EQ(fclose(f), 0);

    EXPECT_EQ(load_config(path), ENPU_SUCCESS);
    EXPECT_EQ(config.memory_request, 32768UL);
    EXPECT_EQ(config.memory_limit, 32768UL);
    remove(path);

    // Restore the global config so later fixtures see a valid one.
    ASSERT_EQ(load_config(MOCK_NPU_CONFIG_PATH), ENPU_SUCCESS);
}

TEST_F(ConfigTest, LoadConfigHighPriorityRejectsIncorrectComputeSettings)
{
    const char *path = "../__build/high_priority.config";
    const char *configs[] = {
        "physical-npu-id=0\nvirtual-npu-id=0\naicore-quota=20\nmemory-quota=32768\n"
        "scheduling-policy=3\nvnpu_priority=1\nshm-id=test-shm\n",
        "physical-npu-id=0\nvirtual-npu-id=0\naicore-quota=0\nmemory-quota=32768\n"
        "scheduling-policy=2\nvnpu_priority=1\nshm-id=test-shm\n",
    };
    for (const char *content : configs) {
        FILE *f = fopen(path, "w");
        ASSERT_NE(f, nullptr);
        ASSERT_GE(fputs(content, f), 0);
        ASSERT_EQ(fclose(f), 0);

        EXPECT_EQ(load_config(path), ENPU_FAIL);
        remove(path);
    }

    ASSERT_EQ(load_config(MOCK_NPU_CONFIG_PATH), ENPU_SUCCESS);
}

TEST_F(ConfigTest, LoadConfigHighPriorityAcceptsCorrectOrOmittedComputeSettings)
{
    const char *path = "../__build/high_priority_valid.config";
    const char *configs[] = {
        "physical-npu-id=0\nvirtual-npu-id=0\naicore-quota=0\nmemory-quota=32768\n"
        "scheduling-policy=3\nvnpu_priority=1\nshm-id=test-shm\n",
        "physical-npu-id=0\nvirtual-npu-id=0\nmemory-quota=32768\n"
        "vnpu_priority=1\nshm-id=test-shm\n",
    };
    for (const char *content : configs) {
        FILE *f = fopen(path, "w");
        ASSERT_NE(f, nullptr);
        ASSERT_GE(fputs(content, f), 0);
        ASSERT_EQ(fclose(f), 0);

        EXPECT_EQ(load_config(path), ENPU_SUCCESS);
        EXPECT_EQ(config.vnpu_priority, VNPU_PRIORITY_HIGH);
        EXPECT_EQ(config.aicore_quota, 0);
        EXPECT_EQ(config.scheduling_policy, 3);
        remove(path);
    }

    ASSERT_EQ(load_config(MOCK_NPU_CONFIG_PATH), ENPU_SUCCESS);
}

TEST_F(ConfigTest, LoadConfigHighPriorityRejectsInvalidComputeSyntax)
{
    const char *path = "../__build/high_priority_invalid.config";
    const char *content = "physical-npu-id=0\nvirtual-npu-id=0\naicore-quota=0.1\n"
                          "memory-quota=32768\nscheduling-policy=3\nvnpu_priority=1\nshm-id=test-shm\n";
    FILE *f = fopen(path, "w");
    ASSERT_NE(f, nullptr);
    ASSERT_GE(fputs(content, f), 0);
    ASSERT_EQ(fclose(f), 0);

    EXPECT_EQ(load_config(path), ENPU_FAIL);
    remove(path);

    ASSERT_EQ(load_config(MOCK_NPU_CONFIG_PATH), ENPU_SUCCESS);
}

// New-style config: request+limit without quota loads fine (enpu-manager generated form).
TEST_F(ConfigTest, LoadConfigRequestLimitWithoutQuota)
{
    const char *path = "../__build/req_limit_no_quota.config";
    const char *content = "physical-npu-id=0\n"
                          "virtual-npu-id=0\n"
                          "aicore-quota=50\n"
                          "memory-request=10240\n"
                          "memory-limit=30720\n"
                          "scheduling-policy=2\n"
                          "shm-id=14422CC3-2040D918-27A73226-80B40A0A-BB100003\n";
    FILE *f = fopen(path, "w");
    ASSERT_NE(f, nullptr);
    ASSERT_EQ(fputs(content, f) >= 0, true);
    ASSERT_EQ(fclose(f), 0);

    EXPECT_EQ(load_config(path), ENPU_SUCCESS);
    EXPECT_EQ(config.memory_request, 10240UL);
    EXPECT_EQ(config.memory_limit, 30720UL);
    remove(path);

    // Restore the global config so later fixtures see a valid one.
    ASSERT_EQ(load_config(MOCK_NPU_CONFIG_PATH), ENPU_SUCCESS);
}

// No memory field at all (no quota, no request/limit) fails the load.
TEST_F(ConfigTest, LoadConfigRejectsNoMemoryConfig)
{
    const char *path = "../__build/no_memory.config";
    const char *content = "physical-npu-id=0\n"
                          "virtual-npu-id=0\n"
                          "aicore-quota=50\n"
                          "scheduling-policy=2\n"
                          "shm-id=14422CC3-2040D918-27A73226-80B40A0A-BB100003\n";
    FILE *f = fopen(path, "w");
    ASSERT_NE(f, nullptr);
    ASSERT_EQ(fputs(content, f) >= 0, true);
    ASSERT_EQ(fclose(f), 0);

    EXPECT_EQ(load_config(path), ENPU_FAIL);
    remove(path);

    // Restore the global config so later fixtures see a valid one.
    ASSERT_EQ(load_config(MOCK_NPU_CONFIG_PATH), ENPU_SUCCESS);
}
