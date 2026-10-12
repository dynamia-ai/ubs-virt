/*
 * Copyright (c) Huawei Technologies Co., Ltd. 2025-2025. All rights reserved.
 */
#include <atomic>
#include <chrono>
#include <cstdio>
#include <fstream>
#include <iostream>
#include <map>
#include <set>
#include <string>
#include <thread>

#include <gtest/gtest.h>
#include <mockcpp/GlobalMockObject.h>
#include <mockcpp/mockcpp.hpp>

#include "server/collector/vcpubind_collector.h"

void VcpuClean()
{
    mockcpp::GlobalMockObject::reset();
}

TEST(VcpuCollectorTest, GetBindingMapSuccess)
{
    std::string data = "\"<domain type='kvm' id='2'>\n<vcpupin vcpu='1' cpuset='0'/>\"";
    std::string cmd = "echo -e " + data;
    FILE *pipe = popen(cmd.c_str(), "r");

    std::map<std::string, std::string> res;
    VcpubindCollector &collector = VcpubindCollector::getInstance();
    MOCKER(popen).stubs().with(any()).will(returnValue(pipe));
    MOCKER(pclose).stubs().with(any()).will(returnValue(0));

    res = collector.get_binding_map("vmname");

    EXPECT_EQ(res["1"], "0");
    VcpuClean();
}

TEST(VcpuCollectorTest, RejectVmNameInjection)
{
    // vm 名里带 shell 元字符时，不能把它拼进命令执行
    std::string marker = "/tmp/ubs_optimizer_vmname_inject";
    std::remove(marker.c_str());

    VcpubindCollector &collector = VcpubindCollector::getInstance();
    std::map<std::string, std::string> res = collector.get_binding_map("vmname; touch " + marker);

    EXPECT_TRUE(res.empty());
    std::ifstream created(marker);
    EXPECT_FALSE(created.good());
    created.close();
    std::remove(marker.c_str());
    VcpuClean();
}

TEST(VcpuCollectorTest, MonitorVmVcpuSuccess)
{
    // 通过echo模拟top输出
    std::string data = "\"PID USER PR NI VIRT RES SHR S %CPU %MEM TIME+ COMMAND P\n"
                       "17751 root 20 0 2337.1g 738016 41476 S 6.7 0.0 12,14 qemu-system-aar 1\"";
    std::string cmd = "echo -e " + data;

    std::map<std::string, std::string> bindingMap = {{"1", "1"}};
    std::atomic<int> count(-1);
    FILE *pipe = popen(cmd.c_str(), "r");
    FILE *nullPipe = nullptr;
    VcpubindCollector &collector = VcpubindCollector::getInstance();

    // mock相关函数
    MOCKER(popen).stubs().with(any()).will(returnValue(pipe)).then(returnValue(nullPipe));
    MOCKER(pclose).stubs().with(any()).will(returnValue(0));

    auto start_time = std::chrono::system_clock::now();
    auto end_time = start_time + std::chrono::seconds(30);
    MOCKER(std::chrono::system_clock::now)
        .stubs()
        .with(any())
        .will(returnValue(start_time))
        .then(returnValue(end_time));
    MOCKER(&VcpubindCollector::get_binding_map).stubs().with(any()).will(returnValue(bindingMap));

    collector.monitor_vm_vpu("openeuler", 10, count, 0.1);
    int res;
    res = count.load(std::memory_order_relaxed);

    EXPECT_EQ(res, 1);
    VcpuClean();
}

TEST(VcpuCollectorTest, ParseTopOutputSkipsMalformedRows)
{
    VcpubindCollector &collector = VcpubindCollector::getInstance();
    collector.pcpu_preempt_count = 0;
    const std::set<std::string> boundCores = {"1"};
    const std::string data = "PID USER %CPU COMMAND P\n"
                             "\n"
                             "100 root\n"
                             "101 root invalid worker 1\n"
                             "102 root 5.0 worker 1\n";

    EXPECT_NO_THROW(collector.parse_top_output(data, boundCores, 1.0));
    EXPECT_EQ(collector.pcpu_preempt_count, 1);
}

TEST(VcpuCollectorTest, SetIndexRejectsMissingRequiredColumns)
{
    VcpubindCollector &collector = VcpubindCollector::getInstance();

    EXPECT_FALSE(collector.set_index("PID USER %CPU COMMAND"));
    EXPECT_TRUE(collector.set_index("PID USER %CPU COMMAND P"));
}

TEST(VcpuCollectorTest, StopInterruptsWaitAndCanBeCalledTwice)
{
    VcpubindCollector &collector = VcpubindCollector::getInstance();
    collector.stop();
    collector.stopRequested_.store(false);
    collector.vcpubindCollectorThread_ = std::make_unique<std::thread>([&collector]() {
        std::unique_lock<std::mutex> lock(collector.waitMutex_);
        collector.stopCondition_.wait_for(lock, std::chrono::hours(1),
                                          [&collector]() { return collector.stopRequested_.load(); });
    });

    collector.stop();
    EXPECT_EQ(collector.vcpubindCollectorThread_.get(), nullptr);
    EXPECT_NO_THROW(collector.stop());
}

TEST(VcpuCollectorTest, LaunchDoesNotReplaceRunningThread)
{
    VcpubindCollector &collector = VcpubindCollector::getInstance();
    collector.stop();
    collector.stopRequested_.store(false);
    collector.vcpubindCollectorThread_ = std::make_unique<std::thread>([&collector]() {
        std::unique_lock<std::mutex> lock(collector.waitMutex_);
        collector.stopCondition_.wait_for(lock, std::chrono::hours(1),
                                          [&collector]() { return collector.stopRequested_.load(); });
    });
    std::thread *runningThread = collector.vcpubindCollectorThread_.get();
    std::atomic<int> result{-1};

    collector.launch("testvm", 1, result);
    EXPECT_EQ(collector.vcpubindCollectorThread_.get(), runningThread);
    collector.stop();
}
