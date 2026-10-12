/*
* Copyright (c) Huawei Technologies Co., Ltd. 2025-2025. All rights reserved.
 */
#include <chrono>
#include <cstdio>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include <sys/statvfs.h>

#include "gtest/gtest.h"
#include "mockcpp/GlobalMockObject.h"
#include "mockcpp/mockcpp.hpp"

#include "server/control/monitor.h"

using namespace mockcpp;

// 替换路径字符串中的文件名
std::string replaceFilename(const std::string &path, const std::string &newFilename)
{
    // 找到最后一个路径分隔符（兼容 Linux '/' 和 Windows '\'）
    size_t lastSepPos = path.find_last_of("/\\");
    // 截取目录部分（分隔符之前的内容），拼接新文件名
    return path.substr(0, lastSepPos + 1) + newFilename;
}

void MonitorClean()
{
    mockcpp::GlobalMockObject::verify();
    mockcpp::GlobalMockObject::reset();
}

TEST(MonitorTest, TestgetGuestNameNull)
{
    MOCKER(popen).expects(once()).will(returnValue(static_cast<FILE *>(nullptr)));
    EXPECT_EQ("", Monitor::getGuestName());
    MonitorClean();
}

TEST(MonitorTest, TestgetGuestName)
{
    std::string filePath = __FILE__;
    FILE *pipe = fopen(replaceFilename(filePath, "normal.txt").c_str(), "r");
    char buffer[255];
    MOCKER(popen).expects(once()).will(returnValue(pipe));
    EXPECT_EQ("vm1", Monitor::getGuestName());
    MonitorClean();
}

TEST(MonitorTest, TestgetGuestNameWithTwoLines)
{
    std::string filePath = __FILE__;
    FILE *pipe = fopen(replaceFilename(filePath, "with_two_lines.txt").c_str(), "r");
    char buffer[255];
    MOCKER(popen).expects(once()).will(returnValue(pipe));
    EXPECT_EQ("", Monitor::getGuestName());
    MonitorClean();
}

TEST(MonitorTest, TestgetGuestNameFalse)
{
    std::string filePath = __FILE__;
    FILE *pipe = fopen(replaceFilename(filePath, "false.txt").c_str(), "r");
    char buffer[255];
    MOCKER(popen).expects(once()).will(returnValue(pipe));
    EXPECT_EQ("", Monitor::getGuestName());
    MonitorClean();
}

TEST(MonitorTest, Testlaunch)
{
    MOCKER(Monitor::getGuestName).expects(once()).will(returnValue(std::string("")));
    std::unique_ptr<Monitor> monitor = std::make_unique<Monitor>();

    monitor->launch();
    MonitorClean();
}

TEST(MonitorTest, TestCheckDisk)
{
    // 情况 1: 模拟 statvfs 失败
    {
        MOCKER(statvfs).expects(once()).will(returnValue(-1)); // 模拟 statvfs 失败
        bool result = Monitor::getInstance().checkDisk(80);
        EXPECT_TRUE(result); // 由于 statvfs 失败，函数返回 true
    }
    MonitorClean();
}

TEST(MonitorTest, TestGetInstance)
{
    // 测试单例实例是否唯一
    Monitor &instance1 = Monitor::getInstance();
    Monitor &instance2 = Monitor::getInstance();
    EXPECT_EQ(&instance1, &instance2); // 确保返回的是同一个实例
    MonitorClean();
}

TEST(MonitorTest, StopInterruptsWaitAndCanBeCalledTwice)
{
    Monitor &monitor = Monitor::getInstance();
    monitor.stop();
    monitor.stopRequested_.store(false);
    monitor.monitorThread_ = std::make_unique<std::thread>([&monitor]() {
        std::unique_lock<std::mutex> lock(monitor.waitMutex_);
        monitor.stopCondition_.wait_for(lock, std::chrono::hours(1),
                                        [&monitor]() { return monitor.stopRequested_.load(); });
    });

    monitor.stop();
    EXPECT_EQ(monitor.monitorThread_.get(), nullptr);
    EXPECT_NO_THROW(monitor.stop());
}

TEST(MonitorTest, LaunchDoesNotReplaceRunningThread)
{
    Monitor &monitor = Monitor::getInstance();
    monitor.stop();
    monitor.stopRequested_.store(false);
    monitor.monitorThread_ = std::make_unique<std::thread>([&monitor]() {
        std::unique_lock<std::mutex> lock(monitor.waitMutex_);
        monitor.stopCondition_.wait_for(lock, std::chrono::hours(1),
                                        [&monitor]() { return monitor.stopRequested_.load(); });
    });
    std::thread *runningThread = monitor.monitorThread_.get();

    monitor.launch();
    EXPECT_EQ(monitor.monitorThread_.get(), runningThread);
    monitor.stop();
}
