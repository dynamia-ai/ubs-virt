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

#ifndef VASD_CONTROLLER_H
#define VASD_CONTROLLER_H

#include <cstdint>
#include <mutex>
#include <thread>

#include "def.h"
#include "socket_server.h"

namespace vas::sched {
using namespace vas::common;
constexpr uint16_t COMPACTION_INTERVAL = 5 * MSECS_PER_SEC; // Default compaction interval

class VasdLooper {
public:
    static VasdLooper &GetInstance()
    {
        static VasdLooper instance;
        return instance;
    }

    static void Run();
    static void Stop();

private:
    static SocketServer server;
    static std::thread eventThread;
    static std::thread timerThread;
    static std::mutex stopMutex;

    static void VmEventHandler();
    static void ClusterCompactionTimer();
    static void StartSocketServer();
};

} // namespace vas::sched

#endif // VASD_CONTROLLER_H
