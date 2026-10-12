/*
 * Copyright (c) Huawei Technologies Co., Ltd. 2025-2025. All rights reserved.
 *
 * ubs-optimizer is licensed under Mulan PSL v2.
 * You can use this software according to the terms and conditions of the Mulan PSL v2.
 * You may obtain a copy of Mulan PSL v2 at:
 *          http://license.coscl.org.cn/MulanPSL2
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND,
 * EITHER EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT,
 * MERCHANTABILITY OR FIT FOR A PARTICULAR PURPOSE.
 * See the Mulan PSL v2 for more details.
 */

#ifndef MONITOR_H
#define MONITOR_H

#include <atomic>
#include <condition_variable>
#include <memory>
#include <mutex>
#include <string>
#include <thread>

class Monitor {
public:
    Monitor(const Monitor &) = delete;
    Monitor &operator=(const Monitor &) = delete;
    static Monitor &getInstance();
    ~Monitor();
    void launch();
    void stop();

private:
    Monitor() = default;

    void mainLoop();
    static bool checkDisk(unsigned int limitPercent);
    static std::string getGuestName();

    std::string guestName;
    std::unique_ptr<std::thread> monitorThread_;
    std::atomic<bool> stopRequested_{false};
    std::mutex lifecycleMutex_;
    std::mutex waitMutex_;
    std::condition_variable stopCondition_;
    unsigned int overloadTimes{0}; // Max value: 3
};

#endif
