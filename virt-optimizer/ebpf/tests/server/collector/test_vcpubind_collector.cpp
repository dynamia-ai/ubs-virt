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