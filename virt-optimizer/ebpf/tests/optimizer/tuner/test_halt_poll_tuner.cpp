/*
 * Copyright (c) Huawei Technologies Co., Ltd. 2025-2025. All rights reserved.
 */
#include <filesystem>
#include <fstream>
#include <iostream>
#include <memory>
#include <vector>

#include <unistd.h>

#include <gtest/gtest.h>
#include <mockcpp/GlobalMockObject.h>
#include <mockcpp/mockcpp.hpp>

#include "common/cmd_executor.h"
#include "optimizer/tuner/halt_poll_tuner.h"

void Mock_Clean()
{
    mockcpp::GlobalMockObject::verify();
    mockcpp::GlobalMockObject::reset();
}

TEST(HaltPollTunerTest, NameTest)
{
    std::shared_ptr<HaltPollTuner> tuner = std::make_shared<HaltPollTuner>();
    EXPECT_EQ("Halt-Poll Configuration", tuner->name());
}

TEST(HaltPollTunerTest, CategoryTest)
{
    std::shared_ptr<HaltPollTuner> tuner = std::make_shared<HaltPollTuner>();
    EXPECT_EQ("IRQ ANOMALY", tuner->category());
}

TEST(HaltPollTunerTest, PrincipleTest)
{
    std::shared_ptr<HaltPollTuner> tuner = std::make_shared<HaltPollTuner>();
    EXPECT_EQ("The overhead caused by the guest automatically entering HLT state during idle periods, "
              "resulting in CPU wake-up time consumption.",
              tuner->principle());
}

TEST(HaltPollTunerTest, AdviceTest)
{
    std::shared_ptr<HaltPollTuner> tuner = std::make_shared<HaltPollTuner>();
    EXPECT_EQ("Enable Haltpoll configuration. Keep the vCPU in a polling state for a period of time when idle, "
              "instead of immediately entering HLT, which can effectively reduce the number of CPU sleeps "
              "and decrease the waiting time of interruption.",
              tuner->advice());
}

TEST(HaltPollTunerTest, CheckFalseTest1)
{
    std::shared_ptr<HaltPollTuner> tuner = std::make_shared<HaltPollTuner>();
    std::string str1 = "Y";
    MOCKER(&CmdExecutor::runCommand).stubs().with(any()).will(returnValue(std::make_pair(true, str1)));
    EXPECT_EQ(false, tuner->check());
    Mock_Clean();
}

TEST(HaltPollTunerTest, CheckFalseTest2)
{
    std::shared_ptr<HaltPollTuner> tuner = std::make_shared<HaltPollTuner>();
    std::string str2 = "xxx";
    std::string filename = "/var/ubs-opt/data/data.json";
    std::ofstream outfile(filename);
    outfile << "{\"timestamp\":123,\"guest_name\":\"\",\"interval\":30,"
               "\"data_table\": {\"ipi_interrupt\":{\"ipi_count\":0,"
               "\"transmission_delay\":12,\"processing_delay\":11},\"host_preempt_vmcore_count\":8}}"
            << std::endl;
    outfile.close();
    MOCKER(&CmdExecutor::runCommand).stubs().with(any()).will(returnValue(std::make_pair(true, str2)));
    MOCKER(HaltPollTuner::decideHaltpoll).stubs().with(any()).will(returnValue(static_cast<uint64_t>(0)));
    EXPECT_EQ(false, tuner->check());
    Mock_Clean();
}

TEST(HaltPollTunerTest, CheckFalseTest3)
{
    std::shared_ptr<HaltPollTuner> tuner = std::make_shared<HaltPollTuner>();
    std::string str2 = "xxx";
    std::string filename = "/var/ubs-opt/data/data.json";
    std::ofstream outfile(filename);
    outfile << "{\"timestamp\":123,\"guest_name\":\"\",\"interval\":30,"
               "\"data_table\": {\"ipi_interrupt\":{\"ipi_count\":0,"
               "\"transmission_delay\":12,\"processing_delay\":11},\"host_preempt_vmcore_count\":8}}"
            << std::endl;
    outfile << "{\"timestamp\":245,\"guest_name\":\"\",\"interval\":30,"
               "\"data_table\": {\"ipi_interrupt\":{\"ipi_count\":10000000,"
               "\"transmission_delay\":12,\"processing_delay\":11}}}"
            << std::endl;
    outfile.close();
    MOCKER(&CmdExecutor::runCommand).stubs().with(any()).will(returnValue(std::make_pair(true, str2)));
    MOCKER(HaltPollTuner::decideHaltpoll).stubs().with(any()).will(returnValue(static_cast<uint64_t>(0)));
    EXPECT_EQ(false, tuner->check());
    Mock_Clean();
}

TEST(HaltPollTunerTest, CheckFalseTest4)
{
    std::shared_ptr<HaltPollTuner> tuner = std::make_shared<HaltPollTuner>();
    std::string str2 = "xxx";
    std::string filename = "/var/ubs-opt/data/data.json";
    std::ofstream outfile(filename);
    outfile << "{\"timestamp\":123,\"guest_name\":\"\",\"interval\":30,"
               "\"data_table\": {\"ipi_interrupt\":{\"ipi_count\":0,"
               "\"transmission_delay\":12,\"processing_delay\":11},\"host_preempt_vmcore_count\":8}}"
            << std::endl;
    outfile << "{\"timestamp\":245,\"guest_name\":\"\",\"interval\":30,"
               "\"data_table\": {\"ipi_interrupt\":{\"ipi_count\":10000000,"
               "\"transmission_delay\":12,\"processing_delay\":11}}}"
            << std::endl;
    outfile.close();
    MOCKER(&CmdExecutor::runCommand).stubs().with(any()).will(returnValue(std::make_pair(true, str2)));
    MOCKER(HaltPollTuner::decideHaltpoll).stubs().with(any()).will(returnValue(static_cast<uint64_t>(1)));
    EXPECT_EQ(true, tuner->check());
    Mock_Clean();
}

TEST(HaltPollTunerTest, CheckFalseTest5)
{
    std::shared_ptr<HaltPollTuner> tuner = std::make_shared<HaltPollTuner>();
    std::string str2 = "xxx";
    MOCKER(&CmdExecutor::runCommand).stubs().with(any()).will(returnValue(std::make_pair(false, str2)));
    EXPECT_EQ(false, tuner->check());
    Mock_Clean();
}

TEST(HaltPollTunerTest, ApplyTrueTest)
{
    std::shared_ptr<HaltPollTuner> tuner = std::make_shared<HaltPollTuner>();
    std::string str3 = "Y";
    MOCKER(&CmdExecutor::runCommand).stubs().with(any()).will(returnValue(std::make_pair(true, str3)));
    testing::internal::CaptureStdout();
    tuner->apply();
    std::string output = testing::internal::GetCapturedStdout();
    EXPECT_NE(output.find("Haltpoll configuration enabled."), std::string::npos);
    Mock_Clean();
}

TEST(HaltPollTunerTest, ApplyFalseTest)
{
    std::shared_ptr<HaltPollTuner> tuner = std::make_shared<HaltPollTuner>();
    std::string str4 = "xxx";
    MOCKER(&CmdExecutor::runCommand).stubs().with(any()).will(returnValue(std::make_pair(false, str4)));
    testing::internal::CaptureStdout();
    tuner->apply();
    std::string output = testing::internal::GetCapturedStdout();
    EXPECT_NE(output.find("Failed to set haltpoll configuration, due to : xxx"), std::string::npos);
    Mock_Clean();
}

TEST(HaltPollTunerTest, DecideHaltpollCase1)
{
    std::shared_ptr<HaltPollTuner> tuner = std::make_shared<HaltPollTuner>();
    uint64_t maxIPI = 2048;
    uint64_t res = tuner->decideHaltpoll(maxIPI);
    EXPECT_EQ(20000000, res);
    Mock_Clean();
}

TEST(HaltPollTunerTest, DecideHaltpollCase2)
{
    std::shared_ptr<HaltPollTuner> tuner = std::make_shared<HaltPollTuner>();
    uint64_t maxIPI = 1024;
    uint64_t res = tuner->decideHaltpoll(maxIPI);
    EXPECT_EQ(0, res);
    Mock_Clean();
}

TEST(HaltPollTunerTest, ParseIPIDataCase1)
{
    const std::string json = R"({
        "timestamp": 1754904102,
        "data_table": {
            "ipi_interrupt": {
                "ipi_count": 213123123,
                "transmission_delay": 12312312,
                "processing_delay": 213123
            }
        }
    })";

    auto result = HaltPollTuner::parseIPIData(json);
    EXPECT_EQ(result.second, static_cast<time_t>(1754904102));
}

TEST(HaltPollTunerTest, ParseIPIDataCase2)
{
    const std::string json = R"({
        "data_table": {
            "ipi_interrupt": {
                "ipi_count": 213123123,
                "transmission_delay": 12312312,
                "processing_delay": 213123
            }
        }
    })";
    EXPECT_THROW(HaltPollTuner::parseIPIData(json), std::runtime_error);
}

TEST(HaltPollTunerTest, ParseIPIDataCase3)
{
    const std::string json = R"({
        "timestamp": 1754904102,
    })";
    EXPECT_THROW(HaltPollTuner::parseIPIData(json), std::runtime_error);
}

TEST(HaltPollTunerTest, ParseIPIDataCase4)
{
    const std::string json = R"({
        "timestamp": 1754904102,
        "data_table": {
        }
    })";
    EXPECT_THROW(HaltPollTuner::parseIPIData(json), std::runtime_error);
}

TEST(HaltPollTunerTest, ParseIPIDataCase5)
{
    const std::string json = R"({
        "timestamp": 1754904102,
        "data_table": {
            "ipi_interrupt": 100;
        }
    })";
    EXPECT_THROW(HaltPollTuner::parseIPIData(json), std::runtime_error);
}

TEST(HaltPollTunerTest, FindLastInferCase1)
{
    IpiInterrupt ipiInterrupt{};
    ipiInterrupt.ipiCount = 213123123;
    ipiInterrupt.transmissionDelay = 12312312;
    ipiInterrupt.processingDelay = 213123;
    time_t timestamp = 17549;
    MOCKER(&HaltPollTuner::parseIPIData).stubs().will(returnObjectList(std::make_pair(ipiInterrupt, timestamp)));
    std::shared_ptr<HaltPollTuner> tuner = std::make_shared<HaltPollTuner>();
    tuner->findLastInfer();
    Mock_Clean();
}
