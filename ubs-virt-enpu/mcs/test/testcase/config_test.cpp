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
#include <cstring>
#include <string>
#include <vector>

#include "mcs_config.h"

namespace {
std::string ValidConfig()
{
    return "physical-npu-id=2\n"
           "virtual-npu-id=7\n"
           "aicore-quota=50\n"
           "memory-quota=8192\n"
           "shm-id=mcs-config-test\n";
}

class ConfigTest : public testing::Test {
protected:
    void TearDown() override
    {
        if (!path_.empty()) {
            unlink(path_.c_str());
        }
    }

    const char *WriteConfig(const std::string &content)
    {
        char path[] = "/tmp/mcs-config-XXXXXX";
        int fd = mkstemp(path);
        EXPECT_GE(fd, 0);
        if (fd < 0) {
            return nullptr;
        }
        EXPECT_EQ(write(fd, content.data(), content.size()), static_cast<ssize_t>(content.size()));
        close(fd);
        path_ = path;
        return path_.c_str();
    }

    mcs_config_t cfg_{};
    std::string path_;
};
} // namespace

TEST_F(ConfigTest, RejectsInvalidArgumentsAndMissingFile)
{
    EXPECT_EQ(mcs_load_config(nullptr, &cfg_), -1);
    EXPECT_EQ(mcs_load_config("/tmp/mcs-config-does-not-exist", nullptr), -1);
    EXPECT_EQ(mcs_load_config("/tmp/mcs-config-does-not-exist", &cfg_), -1);
}

TEST_F(ConfigTest, LoadsRequiredFieldsAndDefaultsPriority)
{
    ASSERT_EQ(mcs_load_config(WriteConfig("# comment\n\n" + ValidConfig()), &cfg_), 0);
    EXPECT_EQ(cfg_.phy_npu_id, 2);
    EXPECT_EQ(cfg_.vnpu_id, 7);
    EXPECT_EQ(cfg_.aicore_quota, 50);
    EXPECT_EQ(cfg_.memory_quota, 8192);
    EXPECT_EQ(cfg_.vnpu_priority, 0);
    EXPECT_STREQ(cfg_.shm_id, "mcs-config-test");
}

TEST_F(ConfigTest, LoadsPriorityAndTrimsTrailingWhitespace)
{
    ASSERT_EQ(mcs_load_config(WriteConfig(ValidConfig() + "vnpu_priority=1  \r\n"), &cfg_), 0);
    EXPECT_EQ(cfg_.vnpu_priority, 1);
}

TEST_F(ConfigTest, IgnoresLegacyPriority)
{
    ASSERT_EQ(mcs_load_config(WriteConfig(ValidConfig() + "priority=1\n"), &cfg_), 0);
    EXPECT_EQ(cfg_.vnpu_priority, 0);
}

TEST_F(ConfigTest, IgnoresUnknownAndMalformedLines)
{
    std::string content = "invalid line\nunknown-key=value\n" + ValidConfig();
    ASSERT_EQ(mcs_load_config(WriteConfig(content), &cfg_), 0);
    EXPECT_EQ(cfg_.aicore_quota, 50);
}

TEST_F(ConfigTest, RejectsEveryMissingRequiredField)
{
    const std::vector<std::string> keys = {
        "physical-npu-id=2\n", "virtual-npu-id=7\n",       "aicore-quota=50\n",
        "memory-quota=8192\n", "shm-id=mcs-config-test\n",
    };
    for (size_t omitted = 0; omitted < keys.size(); ++omitted) {
        std::string content;
        for (size_t i = 0; i < keys.size(); ++i) {
            if (i != omitted) {
                content += keys[i];
            }
        }
        memset(&cfg_, 0x5a, sizeof(cfg_));
        EXPECT_EQ(mcs_load_config(WriteConfig(content), &cfg_), -1) << "omitted index " << omitted;
        mcs_config_t zero{};
        EXPECT_EQ(memcmp(&cfg_, &zero, sizeof(cfg_)), 0);
        TearDown();
        path_.clear();
    }
}

TEST_F(ConfigTest, RejectsInvalidNumericValues)
{
    const std::vector<std::pair<std::string, std::string>> invalid = {
        {"physical-npu-id", "-1"}, {"physical-npu-id", "abc"}, {"physical-npu-id", "1x"},      {"virtual-npu-id", "-1"},
        {"virtual-npu-id", "100"}, {"virtual-npu-id", ""},     {"aicore-quota", "0"},          {"aicore-quota", "-1"},
        {"aicore-quota", "101"},   {"aicore-quota", "50%"},    {"aicore-quota", "2147483648"}, {"memory-quota", "0"},
        {"memory-quota", "-1"},    {"memory-quota", "8192MB"}, {"memory-quota", "abc"},        {"vnpu_priority", "-1"},
        {"vnpu_priority", "2"},    {"vnpu_priority", "high"},
    };
    for (const auto &item : invalid) {
        std::string content = ValidConfig();
        std::string prefix = item.first + "=";
        size_t pos = content.find(prefix);
        if (pos != std::string::npos) {
            size_t end = content.find('\n', pos);
            content.replace(pos, end - pos, prefix + item.second);
        } else {
            content += prefix + item.second + "\n";
        }
        EXPECT_EQ(mcs_load_config(WriteConfig(content), &cfg_), -1) << prefix << item.second;
        mcs_config_t zero{};
        EXPECT_EQ(memcmp(&cfg_, &zero, sizeof(cfg_)), 0);
        TearDown();
        path_.clear();
    }
}

TEST(CalcLimitedAicoreTest, HandlesBoundariesAndRoundsUpToOne)
{
    EXPECT_EQ(mcs_calc_limited_aicore(0, 50), 0);
    EXPECT_EQ(mcs_calc_limited_aicore(-1, 50), 0);
    EXPECT_EQ(mcs_calc_limited_aicore(32, 0), 0);
    EXPECT_EQ(mcs_calc_limited_aicore(32, -1), 0);
    EXPECT_EQ(mcs_calc_limited_aicore(1, 1), 1);
    EXPECT_EQ(mcs_calc_limited_aicore(32, 1), 1);
    EXPECT_EQ(mcs_calc_limited_aicore(32, 50), 16);
    EXPECT_EQ(mcs_calc_limited_aicore(32, 100), 32);
}
