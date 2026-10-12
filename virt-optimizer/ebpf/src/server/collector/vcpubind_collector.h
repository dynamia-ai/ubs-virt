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

#ifndef VCPUBIND_COLLECTOR_H
#define VCPUBIND_COLLECTOR_H

#include <atomic>
#include <condition_variable>
#include <map>
#include <memory>
#include <mutex>
#include <set>
#include <string>
#include <thread>
#include <vector>

class VcpubindCollector {
public:
    static VcpubindCollector &getInstance();

    VcpubindCollector(const VcpubindCollector &) = delete;

    VcpubindCollector &operator=(const VcpubindCollector &) = delete;

    ~VcpubindCollector();

    void launch(std::string vmName, int timeout, std::atomic<int> &res, double minCpu = 1);
    void stop();

private:
    VcpubindCollector() = default;

    void monitor_vm_vpu(std::string vmName, int timeout, std::atomic<int> &res, double minCpu = 1);
    void process_line(const std::string &line, std::map<std::string, std::string> &binding);
    void parse_top_output(const std::string &topOutput, const std::set<std::string> &boundCores, double minCpu);
    bool set_index(const std::string &line);
    std::map<std::string, std::string> get_binding_map(std::string vmName);

    std::unique_ptr<std::thread> vcpubindCollectorThread_;
    std::atomic<bool> stopRequested_{false};
    std::mutex lifecycleMutex_;
    std::mutex waitMutex_;
    std::condition_variable stopCondition_;

    std::map<std::string, std::string> bindmap;
    int pcpu_preempt_count = 0;
    int pid_index = -1;
    int cpupercent_index = -1;
    int command_index = -1;
    int pcpu_index = -1;
};

#endif
