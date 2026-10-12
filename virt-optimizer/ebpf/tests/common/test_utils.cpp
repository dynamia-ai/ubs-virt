/*
 * Copyright (c) Huawei Technologies Co., Ltd. 2025-2025. All rights reserved.
 */
#include <filesystem>
#include <fstream>
#include <iostream>
#include <sstream>
#include <vector>

#include <unistd.h>

#include <gtest/gtest.h>
#include <mockcpp/GlobalMockObject.h>
#include <mockcpp/mockcpp.hpp>
#include "rapidjson/document.h"
#include "rapidjson/error/en.h"
#include "rapidjson/filereadstream.h"

#include "common/utils.h"

namespace {
constexpr const char *CONFIG_TEST_PATH = "/usr/local/sbin/ubs-optimizer/config.json";

class ConfigUtilsTest : public ::testing::Test {
protected:
    void SetUp() override
    {
        std::ifstream input(CONFIG_TEST_PATH);
        configExisted = input.is_open();
        if (configExisted) {
            std::stringstream buffer;
            buffer << input.rdbuf();
            originalConfig = buffer.str();
        }
    }

    void TearDown() override
    {
        if (configExisted) {
            writeConfig(originalConfig);
        } else {
            std::filesystem::remove(CONFIG_TEST_PATH);
        }
    }

    static void writeConfig(const std::string &content)
    {
        std::ofstream output(CONFIG_TEST_PATH, std::ios::trunc);
        ASSERT_TRUE(output.is_open());
        output << content;
        output.close();
    }

private:
    bool configExisted{false};
    std::string originalConfig;
};
} // namespace

void Clean()
{
    mockcpp::GlobalMockObject::verify();
    mockcpp::GlobalMockObject::reset();
}

TEST(ForkWithDirTest, ForkFailure)
{
    // mockcpp mocking 全局 fork 函数
    MOCKER(fork).expects(once()).will(returnValue(-1));

    pid_t result = utils::forkWithDir("/xxx");
    EXPECT_EQ(result, -1);
    Clean();
}

TEST(ForkWithDirTest, ForkSuccess)
{
    MOCKER(fork).expects(once()).will(returnValue(1));

    pid_t result = utils::forkWithDir("/xxx");
    EXPECT_EQ(result, 1);
    Clean();
}

TEST(ForkWithDirTest, SetsidFailure)
{
    MOCKER(fork).expects(once()).will(returnValue(0));
    MOCKER(setsid).expects(once()).will(returnValue(-1));

    pid_t result = utils::forkWithDir("/xxx");
    EXPECT_EQ(result, -1);
    Clean();
}

TEST(ForkWithDirTest, ChdirFailure)
{
    MOCKER(fork).expects(once()).will(returnValue(0));
    MOCKER(setsid).expects(once()).will(returnValue(1));
    MOCKER(umask).expects(once()).will(returnValue((unsigned int)0));
    MOCKER(chdir).expects(once()).will(returnValue(-1));

    pid_t result = utils::forkWithDir("/xxx");
    EXPECT_EQ(result, -1);
    Clean();
}

TEST(ForkWithDirTest, ForkWithDirSuccess)
{
    MOCKER(fork).expects(once()).will(returnValue(0));
    MOCKER(setsid).expects(once()).will(returnValue(1));
    MOCKER(umask).expects(once()).will(returnValue(0));
    MOCKER(chdir).expects(once()).will(returnValue(1));
    MOCKER(close).stubs().will(returnValue(1));

    pid_t result = utils::forkWithDir("/xxx");
    EXPECT_EQ(result, 0);

    Clean();
}

TEST(GetDaemonPIDTest, FileDoesNotExist)
{
    const char *nonExistentFile = "nonexist.pid";
    EXPECT_FALSE(utils::getDaemonPID(nonExistentFile).has_value());
}

TEST(GetDaemonPIDTest, RejectsMalformedAndUnsafePids)
{
    const char *pidFilePath = "test-invalid.pid";
    for (const std::string &content : {"", "abc", "1", "-2", "123abc", "123 456"}) {
        std::ofstream pidFile(pidFilePath, std::ios::trunc);
        pidFile << content;
        pidFile.close();
        EXPECT_FALSE(utils::getDaemonPID(pidFilePath).has_value()) << content;
    }
    std::filesystem::remove(pidFilePath);
}

TEST(GetDaemonPIDTest, ReadsValidPid)
{
    const char *pidFilePath = "test-valid.pid";
    std::ofstream pidFile(pidFilePath, std::ios::trunc);
    pidFile << "123\n";
    pidFile.close();
    ASSERT_TRUE(utils::getDaemonPID(pidFilePath).has_value());
    EXPECT_EQ(utils::getDaemonPID(pidFilePath).value(), 123);
    std::filesystem::remove(pidFilePath);
}

TEST(VmNameValidationTest, AcceptsAlphaNumericNames)
{
    EXPECT_TRUE(utils::isValidVmName("vm1"));
    EXPECT_TRUE(utils::isValidVmName("openEuler2403"));
}

TEST(VmNameValidationTest, RejectsNonAlphaNumericNames)
{
    EXPECT_FALSE(utils::isValidVmName(""));
    EXPECT_FALSE(utils::isValidVmName("-vm"));
    EXPECT_FALSE(utils::isValidVmName("vm-name"));
    EXPECT_FALSE(utils::isValidVmName("vm_name"));
    EXPECT_FALSE(utils::isValidVmName("vm.name"));
    EXPECT_FALSE(utils::isValidVmName("vm;id"));
    EXPECT_FALSE(utils::isValidVmName("vm name"));
    EXPECT_FALSE(utils::isValidVmName("$(id)"));
}

TEST(CheckRunningTest, CheckRunningSuccess)
{
    MOCKER(std::filesystem::exists).stubs().will(returnValue(0));

    bool result = utils::checkRunning("/xxx", "name");
    EXPECT_EQ(result, false);
    Clean();
}

TEST_F(ConfigUtilsTest, GetConfigValuesSuccess)
{
    writeConfig(R"({
        "sampling_interval": 30,
        "vm_name": "openeuler",
        "npu_type": "d802",
        "system": {"ipi_collector": "enable"}
    })");

    const auto interval = utils::getConfigUint("sampling_interval");
    const auto vmName = utils::getConfigString("vm_name");
    const auto collectorFlag = utils::getConfigString("system.ipi_collector");

    ASSERT_TRUE(interval.has_value());
    EXPECT_EQ(interval.value(), 30U);
    ASSERT_TRUE(vmName.has_value());
    EXPECT_EQ(vmName.value(), "openeuler");
    ASSERT_TRUE(collectorFlag.has_value());
    EXPECT_EQ(collectorFlag.value(), "enable");
    EXPECT_EQ(utils::getVmName(), "openeuler");
    EXPECT_EQ(utils::getNPUType(), NPUType::ALLOWED_NPU_D802);
}

TEST_F(ConfigUtilsTest, ReturnedStringOwnsItsStorage)
{
    writeConfig(R"({"vm_name": "openeuler"})");

    const auto vmName = utils::getConfigString("vm_name");
    writeConfig(R"({"vm_name": "changed"})");

    ASSERT_TRUE(vmName.has_value());
    EXPECT_EQ(vmName.value(), "openeuler");
}

TEST_F(ConfigUtilsTest, RejectsInvalidJsonMissingKeysAndInvalidPaths)
{
    writeConfig("{ invalid json }");
    EXPECT_FALSE(utils::getConfigString("vm_name").has_value());

    writeConfig(R"({"vm_name": "openeuler"})");
    EXPECT_FALSE(utils::getConfigString("missing").has_value());
    EXPECT_FALSE(utils::getConfigString(".vm_name").has_value());
    EXPECT_FALSE(utils::getConfigString("vm_name.").has_value());
    EXPECT_FALSE(utils::getConfigString("").has_value());
}

TEST_F(ConfigUtilsTest, RejectsTypeMismatchAndNegativeInteger)
{
    writeConfig(R"({
        "sampling_interval": -1,
        "bind_port": "10101",
        "vm_name": 123
    })");

    EXPECT_FALSE(utils::getConfigUint("sampling_interval").has_value());
    EXPECT_FALSE(utils::getConfigUint("bind_port").has_value());
    EXPECT_FALSE(utils::getConfigString("vm_name").has_value());
}
