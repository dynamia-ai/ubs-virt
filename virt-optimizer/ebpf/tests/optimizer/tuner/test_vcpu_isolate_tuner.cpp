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
#include "optimizer/tuner/vcpu_isolate_tuner.h"

TEST(VCPUIsolTunerTest, GetName)
{
    VCPUIsolTuner tuner;
    EXPECT_EQ(tuner.name(), "Exclusive vCPU");
}

TEST(VCPUIsolTunerTest, GetCategory)
{
    VCPUIsolTuner tuner;
    EXPECT_EQ(tuner.category(), "CPU BOUND");
}

TEST(VCPUIsolTunerTest, GetPrinciple)
{
    VCPUIsolTuner tuner;
    EXPECT_EQ(tuner.principle(), "Frequent preemption of the physical machine's processes on the CPU allocated to "
                                 "the virtual machine leads to CPU-side bubbles or slow execution.");
}

TEST(VCPUIsolTunerTest, GetAdvice)
{
    VCPUIsolTuner tuner;
    EXPECT_EQ(tuner.advice(),
              "Enable Exclusive vCPU. The vCPU can only be scheduled within the allocated virtual machine.");
}

TEST(VCPUIsolTunerTest, ApplyAdvice)
{
    testing::internal::CaptureStdout();
    VCPUIsolTuner tuner;
    tuner.apply();
    std::string output = testing::internal::GetCapturedStdout();
    EXPECT_NE(output.find("Enable Exclusive vCPU"), std::string::npos);
}

TEST(VCPUIsolTunerTest, ParseInvalidJson)
{
    const std::string invalidJson = {"invalid_key"};
    VCPUIsolTuner tuner;
    EXPECT_THROW(tuner.parseHostData(invalidJson), std::runtime_error);
}

TEST(VCPUIsolTunerTest, ParseValidJsonCase1)
{
    const std::string validJson = R"({
        "guest_name": "test",
        "data_table": {
            "qemu_migration_count": 10
        },
        "interval": 100
    })";
    VCPUIsolTuner tuner;
    EXPECT_NO_THROW(tuner.parseHostData(validJson));
}

TEST(VCPUIsolTunerTest, ParseValidJsonCase2)
{
    const std::string validJson = R"({
        "guest_name": "test",
        "data_table": {
            "qemu_migration_count": 10
        },
        "interval": -1
    })";
    VCPUIsolTuner tuner;
    EXPECT_THROW(tuner.parseHostData(validJson), std::runtime_error);
}

TEST(VCPUIsolTunerTest, ParseInvalidJsonCaseWithoutGuestname)
{
    const std::string validJson = R"({
        "data_table": {
            "host_preempt_vmcore_count": 10
        },
        "interval": 100
    })";
    VCPUIsolTuner tuner;
    EXPECT_THROW(tuner.parseHostData(validJson), std::runtime_error);
}

TEST(VCPUIsolTunerTest, ParseInvalidJsonWithoutInterval)
{
    const std::string validJson = R"({
        "guest_name": "test",
        "data_table": {
            "host_preempt_vmcore_count": 10
        }
    })";
    VCPUIsolTuner tuner;
    EXPECT_THROW(tuner.parseHostData(validJson), std::runtime_error);
}

TEST(VCPUIsolTunerTest, ParseValidJsonWithStrInterval)
{
    const std::string validJson = R"({
        "guest_name": "test",
        "data_table": {
            "host_preempt_vmcore_count": 10
        },
        "interval": "100"
    })";
    VCPUIsolTuner tuner;
    EXPECT_THROW(tuner.parseHostData(validJson), std::runtime_error);
}

TEST(VCPUIsolTunerTest, ParseValidJsonWithIntervalIsZero)
{
    const std::string validJson = R"({
        "guest_name": "test",
        "data_table": {
            "host_preempt_vmcore_count": 10
        },
        "interval": 0
    })";
    VCPUIsolTuner tuner;
    EXPECT_THROW(tuner.parseHostData(validJson), std::runtime_error);
}

TEST(VCPUIsolTunerTest, ParseRejectsIntervalAboveConfiguredMaximum)
{
    const std::string json = R"({
        "guest_name": "test",
        "data_table": {"host_preempt_vmcore_count": 10},
        "interval": 601
    })";
    VCPUIsolTuner tuner;
    EXPECT_THROW(tuner.parseHostData(json), std::runtime_error);
}

TEST(VCPUIsolTunerTest, ParseRejectsNegativePreemptCount)
{
    const std::string json = R"({
        "guest_name": "test",
        "data_table": {"host_preempt_vmcore_count": -1},
        "interval": 30
    })";
    VCPUIsolTuner tuner;
    EXPECT_THROW(tuner.parseHostData(json), std::runtime_error);
}

TEST(VCPUIsolTunerTest, CheckCase1)
{
    VCPUIsolTuner tuner;
    std::string cmdOutput = "";
    MOCKER(&CmdExecutor::runCommand).stubs().with(any()).will(returnValue(std::make_pair(false, cmdOutput)));
    EXPECT_TRUE(tuner.check());
    mockcpp::GlobalMockObject::verify();
    mockcpp::GlobalMockObject::reset();
}

TEST(VCPUIsolTunerTest, CheckCase2)
{
    std::string filename = "/var/ubs-opt/data/data.json";
    std::ofstream outfile(filename);
    outfile << "{\"timestamp\":123,\"guest_name\":\"\",\"interval\":30,"
               "\"data_table\": {\"host_preempt_vmcore_count\":200, \"ipi_interrupt\":{\"ipi_count\":0,"
               "\"transmission_delay\":12,\"processing_delay\":11},\"host_preempt_vmcore_count\":8}}"
            << std::endl;
    outfile.close();
    VCPUIsolTuner tuner;
    std::string cmdOutput = "isolcpus=1, nohz_full=2";
    MOCKER(&CmdExecutor::runCommand).stubs().with(any()).will(returnValue(std::make_pair(true, cmdOutput)));
    EXPECT_TRUE(tuner.check());
    mockcpp::GlobalMockObject::verify();
    mockcpp::GlobalMockObject::reset();
}

TEST(VCPUIsolTunerTest, CheckWithValidCmdOutput)
{
    VCPUIsolTuner tuner;
    std::string cmdOutput = "isolcpus=1, nohz_full=2, rcu_nocbs=3";
    MOCKER(&CmdExecutor::runCommand).stubs().with(any()).will(returnValue(std::make_pair(true, cmdOutput)));
    EXPECT_FALSE(tuner.check());
    mockcpp::GlobalMockObject::verify();
    mockcpp::GlobalMockObject::reset();
}
