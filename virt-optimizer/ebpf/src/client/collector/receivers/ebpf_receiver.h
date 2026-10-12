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

#ifndef DATA_COLLECTOR_H
#define DATA_COLLECTOR_H

#include <atomic>
#include <memory>
#include <mutex>
#include <thread>

#include "data_struct.h"

class EBPFReceiver {
public:
    void launch();

    void setSamplingInterval(unsigned int newSamplingInterval);

    void accumulate(const IpiInterrupt &event);
    void accumulate(const SchedSwitch &event);
    void accumulate(const NumaSwitch &event);

    static EBPFReceiver &getInstance();

private:
    static constexpr unsigned int SAMPLING_INTERVAL_DEFAULT = 30;

    [[noreturn]] void mainLoop();
    DataTable takeSnapshot();

    std::atomic<unsigned int> samplingInterval_{SAMPLING_INTERVAL_DEFAULT};
    DataTable dataBuffer_{};
    std::mutex bufferMutex_;

    std::unique_ptr<std::thread> receiverThread_{nullptr};
};

#endif
