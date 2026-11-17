/*
 * Copyright (c) Huawei Technologies Co., Ltd. 2025. All rights reserved.
 *
 * VSched is licensed under the Mulan PSL v2.
 * You can use this software according to the terms and conditions of the Mulan PSL v2.
 * You may obtain a copy of Mulan PSL v2 at:
 *      http://license.coscl.org.cn/MulanPSL2
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY OR FIT FOR A PARTICULAR PURPOSE.
 * See the Mulan PSL v2 for more details.
 */

#include "test_vasd_looper.h"

#include <mockcpp/mockcpp.hpp>

#include "api.h"
#include "cluster_sched.h"
#include "conf.h"
#include "vasd_looper.h"
#include "vm_event_process.h"

namespace vas::ut::vasd {
using namespace vas::sched;

std::atomic<bool> TestVasdLooper::testExitFlag(false);

void TestVasdLooper::SetUp()
{
    Test::SetUp();
}

void TestVasdLooper::TearDown()
{
    Test::TearDown();
    Conf::exitFlag.store(false);
    GlobalMockObject::verify();
}

void ThreadMock()
{
    while (!TestVasdLooper::testExitFlag.load() && !Conf::exitFlag.load()) {
        sleep(1);
    }
}

void SetExitFlagTrue()
{
    sleep(1);
    Conf::exitFlag.store(true);
}

std::string uuid01 = "a1d11347-8738-45fb-8944-e3a058f46401";

VasRet PopMockStart(VmEventInfo &vmEventInfo)
{
    const VmInfo vmInfo{
        .uuid = uuid01,
        .name = "vm01",
        .tgid = 266956,
        .vcpuMap =
            VcpuNumaMap{
                {0, VcpuInfo{266987, {0}, DynamicBitset{}}},
                {1, VcpuInfo{266988, {0}, DynamicBitset{}}},
                {2, VcpuInfo{266989, {0}, DynamicBitset{}}},
            },
    };
    vmEventInfo = VmEventInfo{
        .type = virDomainEventType::VIR_DOMAIN_EVENT_STARTED,
        .detailType = 0,
        .eventType = VmEventType::START,
        .vmInfo = vmInfo,
    };
    return VAS_OK;
}

VasRet PopMockShutdown(VmEventInfo &vmEventInfo)
{
    const VmInfo vmInfo{
        .uuid = uuid01,
        .name = "vm01",
        .tgid = 266956,
        .vcpuMap =
            VcpuNumaMap{
                {0, VcpuInfo{266987, {0}, DynamicBitset{}}},
                {1, VcpuInfo{266988, {0}, DynamicBitset{}}},
                {2, VcpuInfo{266989, {0}, DynamicBitset{}}},
            },
    };
    vmEventInfo = VmEventInfo{
        .type = virDomainEventType::VIR_DOMAIN_EVENT_SHUTDOWN,
        .detailType = 0,
        .eventType = VmEventType::SHUTDOWN,
        .vmInfo = vmInfo,
    };
    return VAS_OK;
}

VasRet SocketMsgHandlerMock(const std::string &cmd, std::string &resStr)
{
    resStr = "success";
    return VAS_OK;
};

TEST_F(TestVasdLooper, testRunAndStop)
{
    MOCKER_CPP(&VmEventProcess::Run, void()).stubs().will(returnValue(nullptr));
    MOCKER_CPP(&VmEventProcess::Stop, void()).stubs().will(returnValue(nullptr));
    MOCKER_CPP(&SocketServer::CloseServer, void (SocketServer::*)()).stubs().will(returnValue(nullptr));
    MOCKER(VasdLooper::VmEventHandler).stubs().will(invoke(ThreadMock));
    MOCKER(VasdLooper::ClusterCompactionTimer).stubs().will(invoke(ThreadMock));
    MOCKER(VasdLooper::StartSocketServer).stubs().will(returnValue(nullptr));
    EXPECT_NO_THROW(VasdLooper::Run());
    testExitFlag.store(true);
    EXPECT_NO_THROW(VasdLooper::Stop());
    EXPECT_NO_THROW(VasdLooper::Stop());
}

TEST_F(TestVasdLooper, testVmEventHandler)
{
    MOCKER_CPP(&VmEventProcess::Pop, VasRet(*)(VmEventInfo &))
        .stubs()
        .will(returnValue(VAS_ERROR))
        .then(invoke(PopMockStart))
        .then(invoke(PopMockShutdown));
    VasdLooper::VmEventHandler();
    MOCKER_CPP(&ClusterSched::ReSched, VasRet(ClusterSched::*)(std::vector<VmDomain> &))
        .stubs()
        .will(returnValue(VAS_OK));
    auto tmp = std::thread(SetExitFlagTrue);
    MOCKER(&ClusterSched::UpdateDomainInfoWithoutLock).stubs().will(returnValue(VAS_OK));
    MOCKER(&ClusterSched::ReSched).stubs().will(returnValue(VAS_OK));
    EXPECT_NO_THROW(VasdLooper::VmEventHandler());
    tmp.join();
    Conf::exitFlag.store(false);
    MOCKER_CPP(&ClusterSched::GetDomainsByUuid, std::vector<VmDomain>(ClusterSched::*)(const std::string &))
        .stubs()
        .will(returnValue(std::vector<VmDomain>{}));
    MOCKER_CPP(&ClusterSched::ClusterCompaction, void (ClusterSched::*)()).stubs().will(returnValue(nullptr));
    tmp = std::thread(SetExitFlagTrue);
    EXPECT_NO_THROW(VasdLooper::VmEventHandler());
    tmp.join();
    Conf::exitFlag.store(false);
}

TEST_F(TestVasdLooper, testStartSocketServer)
{
    MOCKER_CPP(&SocketServer::StartServer, bool (SocketServer::*)())
        .stubs()
        .will(returnValue(false))
        .then(returnValue(true));
    EXPECT_NO_THROW(VasdLooper::StartSocketServer());
    MOCKER_CPP(&SocketServer::AcceptClient, bool (SocketServer::*)())
        .stubs()
        .will(returnValue(false))
        .then(returnValue(true));
    auto tmp = std::thread(SetExitFlagTrue);
    EXPECT_NO_THROW(VasdLooper::StartSocketServer());
    tmp.join();
    Conf::exitFlag.store(false);
    MOCKER_CPP(&SocketServer::ReceiveMessage, std::string(SocketServer::*)())
        .stubs()
        .will(returnValue(std::string("")))
        .then(returnValue(std::string("aaa")));
    tmp = std::thread(SetExitFlagTrue);
    EXPECT_NO_THROW(VasdLooper::StartSocketServer());
    tmp.join();
    Conf::exitFlag.store(false);
    MOCKER_CPP(&Api::SocketMsgHandler, VasRet(*)(const std::string &, std::string &))
        .stubs()
        .will(invoke(SocketMsgHandlerMock));
    MOCKER_CPP(&SocketServer::SendMessage, bool (SocketServer::*)(const std::string &)).stubs().will(returnValue(true));
    tmp = std::thread(SetExitFlagTrue);
    EXPECT_NO_THROW(VasdLooper::StartSocketServer());
    tmp.join();
    Conf::exitFlag.store(false);
}
} // namespace vas::ut::vasd
