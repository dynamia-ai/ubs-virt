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

#ifndef QEMU_COLLECTOR_H
#define QEMU_COLLECTOR_H

#include <atomic>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

class QEMUCollector {
public:
    static QEMUCollector &getInstance();

    QEMUCollector(const QEMUCollector &) = delete;

    QEMUCollector &operator=(const QEMUCollector &) = delete;

    ~QEMUCollector();

    void launch(std::string vm_name, double timeout, std::atomic<int> &res);
    void stop();

private:
    void monitor_qemu_threads(std::string vm_name, double timeout, std::atomic<int> &res);

    QEMUCollector() = default;

    std::vector<std::string> splitBySpace(const std::string &line);

    int findPIndex(const std::vector<std::string> &tokens);

    std::unique_ptr<std::thread> QEMUCollectorThread_;
    std::atomic<bool> stopRequested_{false};
    std::mutex lifecycleMutex_;

    int cpu_pos = -1;
    int mv_count = -1;
};

#endif
