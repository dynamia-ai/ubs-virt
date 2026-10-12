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

#include "qemu_collector.h"

#include <atomic>
#include <cerrno>
#include <charconv>
#include <chrono>
#include <cstddef>
#include <cstdio>
#include <iostream>
#include <map>
#include <sstream>
#include <string>
#include <system_error>
#include <thread>
#include <utility>
#include <vector>

#include <poll.h>
#include <sys/wait.h>
#include <unistd.h>

#include "log/ebpf_logger_macros.h"
#include "process_pipe.h"

void QEMUCollector::launch(std::string vm_name, double timeout, std::atomic<int> &res)
{
    std::lock_guard<std::mutex> lock(lifecycleMutex_);
    if (QEMUCollectorThread_ && QEMUCollectorThread_->joinable()) {
        EBPF_LOG_WARN("QEMU collector is already running.");
        return;
    }
    stopRequested_.store(false, std::memory_order_release);
    cpu_pos = -1;
    mv_count = -1;
    QEMUCollectorThread_ = std::make_unique<std::thread>(&QEMUCollector::monitor_qemu_threads, this, std::move(vm_name),
                                                         timeout, std::ref(res));
}

QEMUCollector::~QEMUCollector()
{
    stop();
}

void QEMUCollector::stop()
{
    std::lock_guard<std::mutex> lock(lifecycleMutex_);
    stopRequested_.store(true, std::memory_order_release);
    if (QEMUCollectorThread_ && QEMUCollectorThread_->joinable()) {
        QEMUCollectorThread_->join();
    }
    QEMUCollectorThread_.reset();
}

QEMUCollector &QEMUCollector::getInstance()
{
    static QEMUCollector instance;
    return instance;
}

void QEMUCollector::monitor_qemu_threads(std::string vm_name, double timeout, std::atomic<int> &res)
{
    try {
        auto start_time = std::chrono::system_clock::now();
        // Start the continuous monitoring process
        std::string cmd = "vmtop -H -b -d 1";

        std::map<std::string, int> history; // {tid: last_cpu}

        // Open child process
        ProcessPipe pipe(popen(cmd.c_str(), "r"));
        if (!pipe) {
            EBPF_LOG_ERROR("Failed to open pipe");
            return;
        }

        const int pipeFd = fileno(pipe.get());
        if (pipeFd < 0) {
            EBPF_LOG_ERROR("Failed to get the vmtop pipe descriptor.");
            return;
        }
        while (!stopRequested_.load(std::memory_order_acquire)) {
            pollfd descriptor{pipeFd, POLLIN, 0};
            const int pollResult = poll(&descriptor, 1, 200);
            if (pollResult < 0) {
                if (errno == EINTR) {
                    continue;
                }
                EBPF_LOG_ERROR("Failed to poll vmtop output, errno=" + std::to_string(errno));
                break;
            }
            if (pollResult == 0) {
                continue;
            }
            if ((descriptor.revents & (POLLIN | POLLHUP)) == 0) {
                EBPF_LOG_WARN("vmtop output pipe reported an error.");
                break;
            }

            char buffer[512];
            if (!fgets(buffer, sizeof(buffer), pipe.get())) {
                break; // Process ends
            }

            std::string line(buffer);

            if (line.find("VM/task-name") != std::string::npos && cpu_pos == -1) {
                std::vector<std::string> tokens = splitBySpace(line);
                cpu_pos = findPIndex(tokens);
                history[vm_name] = -1;
            }

            if (line.find(vm_name) != std::string::npos && cpu_pos >= 0) {
                std::vector<std::string> tokens = splitBySpace(line);
                const size_t cpuIndex = static_cast<size_t>(cpu_pos);
                if (cpuIndex >= tokens.size()) {
                    EBPF_LOG_WARN("Invalid vmtop output: CPU column is missing.");
                    continue;
                }

                int cpuId = 0;
                const std::string &cpuToken = tokens[cpuIndex];
                const auto result = std::from_chars(cpuToken.data(), cpuToken.data() + cpuToken.size(), cpuId);
                if (result.ec != std::errc() || result.ptr != cpuToken.data() + cpuToken.size() || cpuId < 0) {
                    EBPF_LOG_WARN("Invalid vmtop output: CPU value is not an integer.");
                    continue;
                }
                if (history[vm_name] != cpuId) {
                    history[vm_name] = cpuId;
                    mv_count++;
                }
                continue;
            }

            auto stop_time = std::chrono::system_clock::now();
            auto elapsed_time = std::chrono::duration_cast<std::chrono::seconds>(stop_time - start_time);
            if (elapsed_time.count() > timeout) {
                res.store(mv_count, std::memory_order_release);
                mv_count = 0;
                start_time = std::chrono::system_clock::now();
            }
        }
    } catch (const std::exception &e) {
        EBPF_LOG_ERROR("QEMU collector stopped after an unexpected error: " + std::string(e.what()));
    }
}

std::vector<std::string> QEMUCollector::splitBySpace(const std::string &line)
{
    std::istringstream iss(line);
    std::vector<std::string> tokens;
    std::string token;
    while (iss >> token) { // Automatically skip spaces
        tokens.push_back(token);
    }
    return tokens;
}

int QEMUCollector::findPIndex(const std::vector<std::string> &tokens)
{
    for (size_t i = 0; i < tokens.size(); ++i) {
        if (tokens[i] == "P") {
            return static_cast<int>(i); // Return index
        }
    }
    return -1; // Not found
}
