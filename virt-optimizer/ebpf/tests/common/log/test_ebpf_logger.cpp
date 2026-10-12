/*
 * Copyright (c) Huawei Technologies Co., Ltd. 2025-2025. All rights reserved.
 */
#include <fstream>
#include <thread>
#include <vector>

#include <gtest/gtest.h>
#include "common/log/ebpf_logger.h"

class EbpfLoggerTest : public ::testing::Test {
protected:
    EbpfLogger &logger = EbpfLogger::getInstance();

    void SetUp() override
    {
        logger.cleanup();
    }

    void TearDown() override
    {
        logger.cleanup();
    }
};

TEST_F(EbpfLoggerTest, InitReturnsTrueForValidPath)
{
    bool ret = logger.init("/tmp/test_ebpf.log", EbpfLogger::LogLevel::INFO, false, false, 10 * 1024 * 1024);
    EXPECT_TRUE(ret);
    EXPECT_TRUE(logger.isRunningForTest());
}

TEST_F(EbpfLoggerTest, CleanupWithoutInitIsSafe)
{
    logger.cleanup();
    SUCCEED();
}

TEST_F(EbpfLoggerTest, InitAndCleanup)
{
    bool ret = logger.init("/tmp/test_ebpf.log", EbpfLogger::LogLevel::INFO, false, false, 10 * 1024 * 1024);
    EXPECT_TRUE(ret);
    logger.cleanup();
    EXPECT_FALSE(logger.isRunningForTest());
}

TEST_F(EbpfLoggerTest, LogMessageFilteredOut)
{
    logger.init("/tmp/test_ebpf.log", EbpfLogger::LogLevel::WARNING, false, false, 10 * 1024 * 1024);

    size_t initialSize = logger.getLogQueueSizeForTest();

    // DEBUG < WARNING，不入队
    logger.logMessage(EbpfLogger::LogLevel::DEBUG, "debug message", __FILE__, __FUNCTION__, __LINE__);

    size_t afterSize = logger.getLogQueueSizeForTest();

    EXPECT_EQ(initialSize, afterSize);

    logger.cleanup();
}

TEST_F(EbpfLoggerTest, LevelToStringCase1)
{
    logger.logMessage(EbpfLogger::LogLevel::FATAL, "debug message", __FILE__, __FUNCTION__, __LINE__);
    SUCCEED();
}

TEST_F(EbpfLoggerTest, ConcurrentMessagesAreFlushedBeforeCleanup)
{
    const std::string logPath = "/tmp/test_ebpf_concurrent.log";
    std::filesystem::remove(logPath);
    ASSERT_TRUE(logger.init(logPath, EbpfLogger::LogLevel::INFO, false, false, 10 * 1024 * 1024));

    constexpr int THREAD_COUNT = 4;
    constexpr int MESSAGE_COUNT = 50;
    std::vector<std::thread> writers;
    for (int threadIndex = 0; threadIndex < THREAD_COUNT; ++threadIndex) {
        writers.emplace_back([this, threadIndex]() {
            for (int messageIndex = 0; messageIndex < MESSAGE_COUNT; ++messageIndex) {
                logger.logMessage(EbpfLogger::LogLevel::INFO,
                                  "concurrent-" + std::to_string(threadIndex) + "-" + std::to_string(messageIndex),
                                  __FILE__, __FUNCTION__, __LINE__);
            }
        });
    }
    for (auto &writer : writers) {
        writer.join();
    }
    logger.cleanup();

    std::ifstream logFile(logPath);
    ASSERT_TRUE(logFile);
    size_t lineCount = 0;
    std::string line;
    while (std::getline(logFile, line)) {
        ++lineCount;
    }
    EXPECT_EQ(lineCount, static_cast<size_t>(THREAD_COUNT * MESSAGE_COUNT));
    std::filesystem::remove(logPath);
}
