/*
 * Copyright (c) Huawei Technologies Co., Ltd. 2025-2025. All rights reserved.
 */
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>

#include <gtest/gtest.h>
#include <mockcpp/GlobalMockObject.h>
#include <mockcpp/mockcpp.hpp>

#include "cmd_executor.h"
#include "optimizer/tuner/qemu_isolate_tuner.h"
#include "utils.h"

class QemuIsolTunerTest : public ::testing::Test {
protected:
    void SetUp() override
    {
        tuner = std::make_unique<QemuIsolTuner>();
    }

    std::unique_ptr<QemuIsolTuner> tuner;
};

TEST_F(QemuIsolTunerTest, TestName)
{
    EXPECT_EQ(tuner->name(), "QEMU Process Isolation");
}

TEST_F(QemuIsolTunerTest, TestCategory)
{
    EXPECT_EQ(tuner->category(), "CPU BOUND");
}

TEST_F(QemuIsolTunerTest, TestPrinciple)
{
    EXPECT_EQ(tuner->principle(), "The QEMU process frequently switches CPUs, resulting in significant virtualization "
                                  "overhead.");
}

TEST_F(QemuIsolTunerTest, TestAdvice)
{
    EXPECT_EQ(tuner->advice(), "Enable QEMU process isolation. physical machine and "
                               "virtual machine topology synchronization in pass-through scenarios.");
}

TEST_F(QemuIsolTunerTest, TestApply)
{
    EXPECT_NO_THROW(tuner->apply());
}

TEST_F(QemuIsolTunerTest, TestParseHostDataValidJSON)
{
    std::string validJson = R"({
        "guest_name": "test",
        "data_table": {
            "qemu_migration_count": 10
        },
        "interval": 100
    })";
    EXPECT_NO_THROW(tuner->parseHostData(validJson));
}

TEST_F(QemuIsolTunerTest, TestParseHostDataInvalidJSON)
{
    std::string invalidJson = "{ invalid json }";
    EXPECT_THROW(tuner->parseHostData(invalidJson), std::runtime_error);
}

TEST_F(QemuIsolTunerTest, TestFindLastInferSuccess)
{
    std::string validJson = R"({
        "guest_name": "test",
        "data_table": {
            "qemu_migration_count": 10
        }
    })";
    std::ofstream file("test.json");
    file << validJson;
    file.close();
    EXPECT_NO_THROW(tuner->findLastInfer());
    std::remove("test.json");
}

TEST(QemuIsolTunerTest1, CheckCase1)
{
    std::unique_ptr<QemuIsolTuner> tuner = std::make_unique<QemuIsolTuner>();
    MOCKER(&QemuIsolTuner::checkApply).stubs().will(returnValue(QemuIsolTuner::ResultCode::FALSE));
    EXPECT_FALSE(tuner->check());
    mockcpp::GlobalMockObject::verify();
    mockcpp::GlobalMockObject::reset();
}

TEST(QemuIsolTunerTest1, CheckCase2)
{
    std::string filename = "/var/ubs-opt/data/data.json";
    std::ofstream outfile(filename);
    outfile << "{\"timestamp\":123,\"guest_name\":\"\",\"interval\":30,"
               "\"data_table\": {\"qemu_migration_count\":100,\"ipi_interrupt\":{\"ipi_count\":0,"
               "\"transmission_delay\":12,\"processing_delay\":11},\"host_preempt_vmcore_count\":8}}"
            << std::endl;
    outfile.close();
    std::unique_ptr<QemuIsolTuner> tuner = std::make_unique<QemuIsolTuner>();
    MOCKER(&QemuIsolTuner::checkApply).stubs().will(returnValue(QemuIsolTuner::ResultCode::FALSE));
    EXPECT_TRUE(tuner->check());
    mockcpp::GlobalMockObject::verify();
    mockcpp::GlobalMockObject::reset();
}

TEST(QemuIsolTunerTest1, CheckCase3)
{
    std::string filename = "/usr/local/sbin/ubs-optimizer/config.json";
    std::ofstream outfile(filename);
    outfile << "{\"sampling_interval\": 30,\n"
               "\"bind_port\": 10101,\n"
               "\"vm_name\":,\n"
               "\"npu_type\": \"d802\",\n"
               "\"system\" : {}}"
            << std::endl;
    outfile.close();
    std::unique_ptr<QemuIsolTuner> tuner = std::make_unique<QemuIsolTuner>();
    EXPECT_TRUE(tuner->check());
    mockcpp::GlobalMockObject::verify();
    mockcpp::GlobalMockObject::reset();
}

TEST(QemuIsolTunerTest1, CheckApplyWithResultFalse)
{
    std::string filename = "/usr/local/sbin/ubs-optimizer/config.json";
    std::ofstream outfile(filename);
    outfile << "{\"sampling_interval\": 30,\n"
               "\"bind_port\": 10101,\n"
               "\"vm_name\": \"test\",\n"
               "\"npu_type\": \"d802\",\n"
               "\"system\" : {}}"
            << std::endl;
    outfile.close();
    std::string cmdOutput;
    MOCKER(&CmdExecutor::runCommand).stubs().with(any()).will(returnValue(std::make_pair(false, cmdOutput)));
    std::unique_ptr<QemuIsolTuner> tuner = std::make_unique<QemuIsolTuner>();
    EXPECT_EQ(tuner->checkApply(), QemuIsolTuner::ResultCode::ERROR);
    mockcpp::GlobalMockObject::verify();
    mockcpp::GlobalMockObject::reset();
}

TEST(QemuIsolTunerTest1, CheckApplyWithPidPosIsMinusOne)
{
    std::string filename = "/usr/local/sbin/ubs-optimizer/config.json";
    std::ofstream outfile(filename);
    outfile << "{\"sampling_interval\": 30,\n"
               "\"bind_port\": 10101,\n"
               "\"vm_name\": \"test\",\n"
               "\"npu_type\": \"d802\",\n"
               "\"system\" : {}}"
            << std::endl;
    outfile.close();
    std::string cmdOutput = "VM/task-name";
    MOCKER(&CmdExecutor::runCommand).stubs().with(any()).will(returnValue(std::make_pair(true, cmdOutput)));
    std::unique_ptr<QemuIsolTuner> tuner = std::make_unique<QemuIsolTuner>();
    EXPECT_EQ(tuner->checkApply(), QemuIsolTuner::ResultCode::ERROR);
    mockcpp::GlobalMockObject::verify();
    mockcpp::GlobalMockObject::reset();
}

TEST(QemuIsolTunerTest1, CheckApplyRejectsShortProcessRow)
{
    std::ofstream outfile("/usr/local/sbin/ubs-optimizer/config.json");
    outfile << R"({"vm_name":"test"})" << std::endl;
    outfile.close();
    const std::string cmdOutput = "VM/task-name P PID\ntest\n";
    MOCKER(&CmdExecutor::runCommand).stubs().with(any()).will(returnValue(std::make_pair(true, cmdOutput)));
    QemuIsolTuner tuner;
    EXPECT_EQ(tuner.checkApply(), QemuIsolTuner::ResultCode::ERROR);
    mockcpp::GlobalMockObject::verify();
    mockcpp::GlobalMockObject::reset();
}

TEST(QemuIsolTunerTest1, CheckApplyRejectsNonNumericPid)
{
    std::ofstream outfile("/usr/local/sbin/ubs-optimizer/config.json");
    outfile << R"({"vm_name":"test"})" << std::endl;
    outfile.close();
    const std::string cmdOutput = "VM/task-name P PID\ntest P invalid\n";
    MOCKER(&CmdExecutor::runCommand).stubs().with(any()).will(returnValue(std::make_pair(true, cmdOutput)));
    QemuIsolTuner tuner;
    EXPECT_EQ(tuner.checkApply(), QemuIsolTuner::ResultCode::ERROR);
    mockcpp::GlobalMockObject::verify();
    mockcpp::GlobalMockObject::reset();
}

TEST(QemuIsolTunerTest1, CheckApplyWithIsAllFError)
{
    std::string filename = "/usr/local/sbin/ubs-optimizer/config.json";
    std::ofstream outfile(filename);
    outfile << "{\"sampling_interval\": 30,\n"
               "\"bind_port\": 10101,\n"
               "\"vm_name\": \"test\",\n"
               "\"npu_type\": \"d802\",\n"
               "\"system\" : {}}"
            << std::endl;
    outfile.close();
    std::string cmdOutput;
    MOCKER(&CmdExecutor::runCommand).stubs().with(any()).will(returnValue(std::make_pair(true, cmdOutput)));
    std::unique_ptr<QemuIsolTuner> tuner = std::make_unique<QemuIsolTuner>();
    EXPECT_EQ(tuner->checkApply(), QemuIsolTuner::ResultCode::ERROR);
    mockcpp::GlobalMockObject::verify();
    mockcpp::GlobalMockObject::reset();
}

TEST(QemuIsolTunerTest1, ParseInvalidJsonCaseWithoutGuestname)
{
    const std::string validJson = R"({
        "data_table": {
            "host_preempt_vmcore_count": 10
        },
        "interval": 100
    })";
    QemuIsolTuner tuner;
    EXPECT_THROW(tuner.parseHostData(validJson), std::runtime_error);
}

TEST(QemuIsolTunerTest1, ParseInvalidJsonWithoutInterval)
{
    const std::string validJson = R"({
        "guest_name": "test",
        "data_table": {
            "host_preempt_vmcore_count": 10
        }
    })";
    QemuIsolTuner tuner;
    EXPECT_THROW(tuner.parseHostData(validJson), std::runtime_error);
}

TEST(QemuIsolTunerTest1, ParseValidJsonWithStrInterval)
{
    const std::string validJson = R"({
        "guest_name": "test",
        "data_table": {
            "host_preempt_vmcore_count": 10
        },
        "interval": "100"
    })";
    QemuIsolTuner tuner;
    EXPECT_THROW(tuner.parseHostData(validJson), std::runtime_error);
}

TEST(QemuIsolTunerTest1, ParseValidJsonWithIntervalIsZero)
{
    const std::string validJson = R"({
        "guest_name": "test",
        "data_table": {
            "host_preempt_vmcore_count": 10
        },
        "interval": 0
    })";
    QemuIsolTuner tuner;
    EXPECT_THROW(tuner.parseHostData(validJson), std::runtime_error);
}
