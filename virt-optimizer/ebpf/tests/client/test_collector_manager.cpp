/*
* Copyright (c) Huawei Technologies Co., Ltd. 2025-2025. All rights reserved.
 */
#include <filesystem>
#include <fstream>
#include <string>

#include <gtest/gtest.h>
#include <mockcpp/GlobalMockObject.h>
#include <mockcpp/mockcpp.hpp>

#include "client/collector/collector.h"
#include "client/collector/receivers/ebpf_receiver.h"
#include "client/collector_manager.h"
#include "common/utils.h"

void test() {}
TEST(CollectorManagerTest, LaunchReceiverTest)
{
    CollectorManager &mgr = CollectorManager::getInstance();
    rapidjson::Document doc;
    doc.SetObject();

    EXPECT_FALSE(mgr.launchReceiver(doc)); // sampling_interval 不存在

    doc.SetObject();
    doc.AddMember("sampling_interval", "invalid", doc.GetAllocator());
    EXPECT_FALSE(mgr.launchReceiver(doc)); // sampling_interval 非整数

    doc.SetObject();
    doc.AddMember("sampling_interval", 0, doc.GetAllocator());
    EXPECT_FALSE(mgr.launchReceiver(doc)); // sampling_interval 超出范围

    doc.SetObject();
    doc.AddMember("sampling_interval", 60, doc.GetAllocator());
    MOCKER(&EBPFReceiver::launch).stubs().will(invoke(test));
    EXPECT_TRUE(mgr.launchReceiver(doc)); // sampling_interval 正确
    MOCKER(&EBPFReceiver::launch).reset();
    GlobalMockObject::verify();
    GlobalMockObject::reset();
}

TEST(CollectorManagerTest, GetInstanceTest)
{
    CollectorManager &manager1 = CollectorManager::getInstance();
    CollectorManager &manager2 = CollectorManager::getInstance();
    EXPECT_EQ(&manager1, &manager2);
}

TEST(CollectorManagerTest, LaunchCollectorTest)
{
    CollectorManager &mgr = CollectorManager::getInstance();
    rapidjson::Document doc;

    doc.SetObject();
    EXPECT_FALSE(mgr.launchCollector(doc)); // system 不存在

    doc.SetObject();
    doc.AddMember("system", "invalid", doc.GetAllocator());
    EXPECT_FALSE(mgr.launchCollector(doc)); // system 非对象

    doc.SetObject();
    rapidjson::Value systemObj(rapidjson::kObjectType);
    systemObj.AddMember("invalid_collector", "enable", doc.GetAllocator());
    doc.AddMember("system", systemObj, doc.GetAllocator());
    EXPECT_FALSE(mgr.launchCollector(doc)); // collector 中无有效项

    doc.SetObject();
    rapidjson::Value legacySystemObj(rapidjson::kObjectType);
    legacySystemObj.AddMember("ipi_collection", "enable", doc.GetAllocator());
    doc.AddMember("system", legacySystemObj, doc.GetAllocator());
    EXPECT_FALSE(mgr.launchCollector(doc));

    doc.SetObject();
    rapidjson::Value systemObj2(rapidjson::kObjectType);
    systemObj2.AddMember("ipi_collector", "invalid", doc.GetAllocator());
    doc.AddMember("system", systemObj2, doc.GetAllocator());
    EXPECT_FALSE(mgr.launchCollector(doc)); // ipi_collector 值不是 enable 或 disable

    doc.SetObject();
    rapidjson::Value systemObj3(rapidjson::kObjectType);
    systemObj3.AddMember("ipi_collector", 1, doc.GetAllocator());
    doc.AddMember("system", systemObj3, doc.GetAllocator());
    EXPECT_FALSE(mgr.launchCollector(doc)); // collector配置值不是字符串
}

TEST(CollectorManagerTest, DaemonizeTest)
{
    CollectorManager &mgr = CollectorManager::getInstance();
    mgr.daemonize();

    MOCKER(utils::forkWithDir).expects(once()).will(returnValue(1));
    mgr.daemonize();
    MOCKER(utils::forkWithDir).reset();
    GlobalMockObject::verify();
    GlobalMockObject::reset();
}
