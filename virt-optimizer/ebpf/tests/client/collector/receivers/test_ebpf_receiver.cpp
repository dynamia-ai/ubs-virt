/*
 * Copyright (c) Huawei Technologies Co., Ltd. 2025-2025. All rights reserved.
 */
#include <atomic>
#include <cerrno>
#include <chrono>
#include <ctime>
#include <filesystem>
#include <iostream>
#include <memory>
#include <thread>
#include <vector>

#include <unistd.h>

#include <gtest/gtest.h>
#include <mockcpp/GlobalMockObject.h>
#include <mockcpp/mockcpp.hpp>

#include "client/collector/receivers/ebpf_receiver.h"
#include "client/collector/receivers/vsock_client.h"

TEST(EBPFReceiverTest, PerformanceTest)
{
    pid_t pid = fork();
    if (pid == 0) {
        // 子进程运行 EBPFReceiver
        auto &receiver = EBPFReceiver::getInstance();
        receiver.launch();
        mockcpp::GlobalMockObject::verify();
        mockcpp::GlobalMockObject::reset();
    } else {
        // 父进程控制子进程
        std::this_thread::sleep_for(std::chrono::seconds(1));
        kill(pid, SIGTERM);       // 终止子进程
        waitpid(pid, nullptr, 0); // 等待回收
    }
}

TEST(EBPFReceiverTest, SetSamplingIntervalTest)
{
    // 获取实例
    EBPFReceiver &instance1 = EBPFReceiver::getInstance();
    instance1.setSamplingInterval(10);
    mockcpp::GlobalMockObject::verify();
    mockcpp::GlobalMockObject::reset();
}

TEST(EBPFReceiverTest, GetInstanceTest)
{
    // 获取实例
    EBPFReceiver &instance1 = EBPFReceiver::getInstance();
    EBPFReceiver &instance2 = EBPFReceiver::getInstance();
    EXPECT_EQ(&instance1, &instance2);
}

TEST(EBPFReceiverTest, ConcurrentAccumulateProducesConsistentSnapshot)
{
    EBPFReceiver &receiver = EBPFReceiver::getInstance();
    receiver.takeSnapshot();

    constexpr int threadCount = 4;
    constexpr int iterations = 1000;
    const IpiInterrupt ipiEvent{1, 2, 3};
    const SchedSwitch schedEvent{4, 5};
    const NumaSwitch numaEvent{6, 7};
    std::vector<std::thread> producers;
    for (int index = 0; index < threadCount; ++index) {
        producers.emplace_back([&receiver, &ipiEvent, &schedEvent, &numaEvent]() {
            for (int iteration = 0; iteration < iterations; ++iteration) {
                receiver.accumulate(ipiEvent);
                receiver.accumulate(schedEvent);
                receiver.accumulate(numaEvent);
            }
        });
    }
    for (auto &producer : producers) {
        producer.join();
    }

    const DataTable snapshot = receiver.takeSnapshot();
    const uint64_t eventCount = static_cast<uint64_t>(threadCount * iterations);
    EXPECT_EQ(snapshot.ipiInterrupt.ipiCount, eventCount);
    EXPECT_EQ(snapshot.ipiInterrupt.transmissionDelay, eventCount * 2);
    EXPECT_EQ(snapshot.ipiInterrupt.processingDelay, eventCount * 3);
    EXPECT_EQ(snapshot.schedSwitch.contextSwitchCount, eventCount * 4);
    EXPECT_EQ(snapshot.schedSwitch.cpuMigrationCount, eventCount * 5);
    EXPECT_EQ(snapshot.numaSwitch.numaMoveCount, eventCount * 6);
    EXPECT_EQ(snapshot.numaSwitch.numaSwapCount, eventCount * 7);

    const DataTable clearedSnapshot = receiver.takeSnapshot();
    EXPECT_EQ(clearedSnapshot.ipiInterrupt.ipiCount, 0U);
    EXPECT_EQ(clearedSnapshot.schedSwitch.contextSwitchCount, 0U);
    EXPECT_EQ(clearedSnapshot.numaSwitch.numaMoveCount, 0U);
}
