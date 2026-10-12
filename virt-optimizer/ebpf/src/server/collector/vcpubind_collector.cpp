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

#include "vcpubind_collector.h"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <iostream>
#include <map>
#include <set>
#include <sstream>
#include <string>
#include <thread>
#include <utility>

#include "log/ebpf_logger_macros.h"
#include "process_pipe.h"
#include "utils.h"

void VcpubindCollector::launch(std::string vmName, int timeout, std::atomic<int> &res, double minCpu)
{
    std::lock_guard<std::mutex> lock(lifecycleMutex_);
    if (vcpubindCollectorThread_ && vcpubindCollectorThread_->joinable()) {
        EBPF_LOG_WARN("vCPU binding collector is already running.");
        return;
    }
    stopRequested_.store(false, std::memory_order_release);
    pcpu_preempt_count = 0;
    bindmap.clear();
    vcpubindCollectorThread_ = std::make_unique<std::thread>(&VcpubindCollector::monitor_vm_vpu, this,
                                                             std::move(vmName), timeout, std::ref(res), minCpu);
}

VcpubindCollector::~VcpubindCollector()
{
    stop();
}

void VcpubindCollector::stop()
{
    std::lock_guard<std::mutex> lock(lifecycleMutex_);
    stopRequested_.store(true, std::memory_order_release);
    stopCondition_.notify_all();
    if (vcpubindCollectorThread_ && vcpubindCollectorThread_->joinable()) {
        vcpubindCollectorThread_->join();
    }
    vcpubindCollectorThread_.reset();
}

// 监控主循环
void VcpubindCollector::monitor_vm_vpu(std::string vmName, int timeout, std::atomic<int> &res, double minCpu)
{
    try {
        auto start_time = std::chrono::system_clock::now();
        std::map<std::string, std::string> binding = get_binding_map(vmName);
        if (binding.empty()) {
            EBPF_LOG_ERROR("No binding information found for VM: " + vmName);
            return;
        }

        std::set<std::string> boundCores;
        for (const auto &pair : binding) {
            boundCores.insert(pair.second); // Insert value into set
        }

        while (!stopRequested_.load(std::memory_order_acquire)) {
            std::string cmd = "/usr/bin/top -b -n 1 -o %CPU -w 128";
            ProcessPipe pipe(popen(cmd.c_str(), "r"));
            if (!pipe) {
                EBPF_LOG_ERROR("Failed to open pipe");
                return;
            }

            std::string topOutput;
            char buffer[512];

            char *readResult = fgets(buffer, sizeof(buffer), pipe.get());
            while (readResult != nullptr) {
                topOutput += buffer;
                readResult = fgets(buffer, sizeof(buffer), pipe.get());
            }
            if (ferror(pipe.get()) != 0) {
                EBPF_LOG_WARN("Failed to read top output from pipe.");
                continue;
            }
            int ret = pipe.close();
            if (ret != 0) {
                EBPF_LOG_WARN("Failed to pclose pipe, the ret: " + std::to_string(ret));
                continue;
            }

            // Key processing logic
            parse_top_output(topOutput, boundCores, minCpu);

            auto stopTime = std::chrono::system_clock::now();
            auto elapsedTime = std::chrono::duration_cast<std::chrono::seconds>(stopTime - start_time);

            if (elapsedTime.count() > timeout) {
                res.store(pcpu_preempt_count, std::memory_order_release);
                pcpu_preempt_count = 0;
                start_time = std::chrono::system_clock::now();
            }
            std::unique_lock<std::mutex> lock(waitMutex_);
            stopCondition_.wait_for(lock, std::chrono::seconds(1),
                                    [this]() { return stopRequested_.load(std::memory_order_acquire); });
        }
    } catch (const std::exception &e) {
        EBPF_LOG_ERROR("vCPU binding collector stopped after an unexpected error: " + std::string(e.what()));
    }
}

// Get the binding dictionary
std::map<std::string, std::string> VcpubindCollector::get_binding_map(std::string vmName)
{
    bindmap.clear(); // bindmap 是成员变量，避免上一次调用的结果残留
    // vmName 会被拼进 shell 命令，只允许字母和数字
    if (!utils::isValidVmName(vmName)) {
        EBPF_LOG_ERROR("Invalid vm name, only English letters or numbers are supported.");
        return bindmap;
    }
    std::string cmd = "virsh dumpxml " + vmName;
    ProcessPipe pipe(popen(cmd.c_str(), "r"));

    if (!pipe) {
        EBPF_LOG_ERROR("Failed to open pipe");
        return bindmap;
    }

    while (true) {
        char buffer[512];
        if (!fgets(buffer, sizeof(buffer), pipe.get())) {
            break; // Process ends
        }
        std::string line(buffer);

        process_line(line, bindmap);
    }

    return bindmap;
}

// Parse top output
void VcpubindCollector::parse_top_output(const std::string &topOutput, const std::set<std::string> &boundCores,
                                         double minCpu)
{
    std::istringstream iss(topOutput);
    std::string line;

    bool in_header = true;
    while (std::getline(iss, line)) {
        if (in_header) {
            if (line.find("PID") != std::string::npos && line.find("USER") != std::string::npos) {
                if (!set_index(line)) {
                    EBPF_LOG_WARN("Invalid top output: required columns are missing.");
                    return;
                }
                in_header = false;
            }
            continue;
        }
        std::istringstream line_stream(line);
        std::vector<std::string> fields;
        std::string field;
        while (line_stream >> field) {
            fields.push_back(field);
        }
        const size_t maxIndex = static_cast<size_t>(std::max({cpupercent_index, pcpu_index, command_index}));
        if (fields.size() <= maxIndex) {
            EBPF_LOG_WARN("Invalid top output: process row has too few columns.");
            continue;
        }
        try {
            if ((std::stod(fields[static_cast<size_t>(cpupercent_index)]) > minCpu) &&
                (boundCores.count(fields[static_cast<size_t>(pcpu_index)]) != 0) &&
                (fields[static_cast<size_t>(command_index)] != "top")) {
                pcpu_preempt_count++;
            }
        } catch (const std::exception &) {
            EBPF_LOG_WARN("Invalid top output: CPU usage is not a number.");
        }
    }
}

// Get the index of the required element in top
bool VcpubindCollector::set_index(const std::string &line)
{
    std::istringstream line_stream(line);
    std::vector<std::string> fields;
    std::string field;
    while (line_stream >> field) {
        fields.push_back(field);
    }

    pid_index = -1;
    cpupercent_index = -1;
    command_index = -1;
    pcpu_index = -1;
    for (int i = 0; i < static_cast<int>(fields.size()); i++) {
        if (fields[i] == "PID") {
            pid_index = i;
        } else if (fields[i] == "COMMAND") {
            command_index = i;
        } else if (fields[i] == "%CPU") {
            cpupercent_index = i;
        } else if (fields[i] == "P") {
            pcpu_index = i;
        }
    }
    return pid_index >= 0 && cpupercent_index >= 0 && command_index >= 0 && pcpu_index >= 0;
}

// Process a single line,extract vcpu and pcpu and add them to the binding dictionary
void VcpubindCollector::process_line(const std::string &line, std::map<std::string, std::string> &binding)
{
    if (line.find("<vcpupin") != std::string::npos && line.find("cpuset") != std::string::npos) {
        std::string vcpu;
        std::string pcpu;

        // Extract vCPU
        size_t vcpu_start = line.find("vcpu='") + 6; // 6 是 "vcpu='" 的长度
        size_t vcpu_end = line.find("'", vcpu_start);
        if (vcpu_start != std::string::npos && vcpu_end != std::string::npos) {
            vcpu = line.substr(vcpu_start, vcpu_end - vcpu_start);
        }

        // Extract pcpu
        size_t pcpu_start = line.find("cpuset='") + 8; // 8 是 "cpuset='" 的长度
        size_t pcpu_end = line.find("'", pcpu_start);
        if (pcpu_start != std::string::npos && pcpu_end != std::string::npos) {
            pcpu = line.substr(pcpu_start, pcpu_end - pcpu_start);
        }

        if (!vcpu.empty() && !pcpu.empty()) {
            binding[vcpu] = pcpu;
        }
    }
}

VcpubindCollector &VcpubindCollector::getInstance()
{
    static VcpubindCollector instance;
    return instance;
}
