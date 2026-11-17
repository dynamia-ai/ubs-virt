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

#include "vasd_looper.h"

#include <pthread.h>
#include <signal.h>
#include <atomic>

#include "api.h"
#include "cluster_sched.h"
#include "conf.h"
#include "libvirt_helper.h"
#include "logger.h"
#include "socket_server.h"
#include "vm_event_process.h"

namespace vas::sched {
SocketServer VasdLooper::server{};
std::thread VasdLooper::eventThread{};
std::thread VasdLooper::timerThread{};
std::mutex VasdLooper::stopMutex{};
constexpr int16_t THREAD_SLEEP = 100;

void VasdLooper::Run()
{
    sigset_t exitSignalSet{};
    sigemptyset(&exitSignalSet);
    sigaddset(&exitSignalSet, SIGINT);
    sigaddset(&exitSignalSet, SIGTERM);
    sigset_t previousSignalSet{};
    if (pthread_sigmask(SIG_BLOCK, &exitSignalSet, &previousSignalSet) != 0) {
        LOG_ERROR("Failed to block daemon exit signals.");
        return;
    }

    // Listening to VM events
    VmEventProcess::Run();
    // Handling Virtual Machine Queue Events
    eventThread = std::thread(&VmEventHandler);
    // Periodically organize the CPU
    timerThread = std::thread(&ClusterCompactionTimer);
    std::atomic<bool> signalReceived{false};
    std::thread signalThread([&exitSignalSet, &signalReceived]() {
        int receivedSignal = 0;
        if (sigwait(&exitSignalSet, &receivedSignal) != 0) {
            LOG_ERROR("Failed to wait for daemon exit signal.");
            return;
        }
        signalReceived.store(true);
        LOG_INFO("Received signal " + std::to_string(receivedSignal));
        Stop();
    });
    // socketServer start
    StartSocketServer();

    if (!signalReceived.load()) {
        pthread_kill(signalThread.native_handle(), SIGTERM);
    }
    signalThread.join();
    pthread_sigmask(SIG_SETMASK, &previousSignalSet, nullptr);
}

void VasdLooper::Stop()
{
    Conf::exitFlag.store(true);
    std::lock_guard<std::mutex> lock(stopMutex);
    VmEventProcess::Stop();
    server.CloseServer();
    // join thread wait for exit
    if (eventThread.joinable()) {
        eventThread.join();
    }
    if (timerThread.joinable()) {
        timerThread.join();
    }
}

/**
 * Virtual machine event queue triggers CPU scheduling
 */
void VasdLooper::VmEventHandler()
{
    LOG_INFO("Start to listen vm event queue");
    while (!Conf::exitFlag.load()) {
        try {
            VmEventInfo vmEventInfo{};
            if (const auto ret = VmEventProcess::Pop(vmEventInfo); isVasRetFail(ret)) {
                break;
            }
            LOG_INFO("Vm event comming. eventType=" + std::to_string(static_cast<uint32_t>(vmEventInfo.eventType)));
            switch (vmEventInfo.eventType) {
                case VmEventType::START: {
                    ClusterSched::GetInstance().AddDomainInfo(vmEventInfo.vmInfo);
                    break;
                }
                case VmEventType::SHUTDOWN: {
                    ClusterSched::GetInstance().DelDomainInfo(vmEventInfo.vmInfo.uuid);
                    break;
                }
                default: {
                    break;
                }
            }
            LOG_INFO("Vm event handler end. Wait next event.");
        } catch (const std::exception &e) {
            LOG_ERROR("Vm event handler error: " + std::string(e.what()));
        }
    }
    LOG_INFO("Exit flag is true. vm event handler exit.");
}

/**
 * Periodically perform cpu compaction
 */
void VasdLooper::ClusterCompactionTimer()
{
    LOG_INFO("Start to run cluster compaction looper");
    while (!Conf::exitFlag.load()) {
        try {
            LOG_INFO("Start compaction.");
            if (isVasRetFail(ClusterSched::GetInstance().UpdateDomainInfosAndSched())) {
                LOG_ERROR("Update domain infos and sched failed, skip compaction this round.");
            } else {
                // Compact fragment
                ClusterSched::GetInstance().ClusterCompaction();
            }
        } catch (const std::exception &e) {
            LOG_ERROR("Cluster compaction timer error: " + std::string(e.what()));
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(COMPACTION_INTERVAL));
    }
    VmInfoMap vmInfoMap{};
    LibvirtHelper::GetCache(vmInfoMap);
    ClusterSched::GetInstance().RecoverVmVcpu(vmInfoMap);
    LOG_INFO("Exit flag is true. Cluster compaction timer exit.");
}

void VasdLooper::StartSocketServer()
{
    try {
        if (!server.StartServer()) {
            LOG_ERROR("Failed to start server on uds.");
            return;
        }
        LOG_INFO("Server started. Waiting for client connection.");

        while (!Conf::exitFlag.load()) {
            if (!server.AcceptClient()) {
                LOG_ERROR("Failed to accept client connection");
                continue;
            }
            LOG_INFO("Client connected successfully");
            std::string receivedMsg = server.ReceiveMessage();
            std::string response{};
            if (receivedMsg.empty()) {
                LOG_ERROR("No message received or connection closed");
            } else {
                LOG_INFO("Received message: " + receivedMsg);
            }
            auto ret = Api::SocketMsgHandler(receivedMsg, response);
            SocketResponse resp;
            resp.retCode = ret;
            resp.retMsg = response;
            server.SendMessage(resp.ToString());
            std::this_thread::sleep_for(std::chrono::milliseconds(THREAD_SLEEP));
        }
    } catch (const std::exception &e) {
        LOG_ERROR("Server error: " + std::string(e.what()));
        return;
    }
    LOG_INFO("Exit flag is true. Socket server exit.");
}
} // namespace vas::sched
