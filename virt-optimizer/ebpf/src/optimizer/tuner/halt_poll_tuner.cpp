/*
 * Copyright (c) Huawei Technologies Co., Ltd. 2025-2025. All rights reserved.
 *
 * ubs-optimizer is licensed under Mulan PSL v2.
 * You can use this software according to the terms and conditions of the Mulan PSL v2.
 * You may obtain a copy of Mulan PSL v2 at:
 * http://license.coscl.org.cn/MulanPSL2
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND,
 * EITHER EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT,
 * MERCHANTABILITY OR FIT FOR A PARTICULAR PURPOSE.
 * See the Mulan PSL v2 for more details.
 */

#include "halt_poll_tuner.h"

#include <algorithm>
#include <chrono>
#include <filesystem>
#include <iostream>
#include <queue>

#include "rapidjson/error/en.h"

#include "cmd_executor.h"
#include "data_file_snapshot.h"
#include "log/ebpf_logger_macros.h"
#include "utils.h"

const std::string_view DATA_PATH = "/var/ubs-opt/data/";
const std::string_view DATA_FILE_NAME = "data.json";
const uint64_t INFER_THRESHOLD = 400;        // Ipi count > 300/s => inference
const uint64_t MAX_DATA_SIZE = 100;          // Max data size for inference
const uint32_t MAX_ACCEPTED_INTERVAL = 1000; // Data interval greater than this will not be considered

std::string HaltPollTuner::name() const
{
#if defined(__aarch64__)
    return "Halt-Poll Configuration";
#elif defined(__x86_64__)
    return "Optimizer Configuration";
#endif
}

std::string HaltPollTuner::category() const
{
    return "IRQ ANOMALY";
}

std::string HaltPollTuner::principle() const
{
    return "The overhead caused by the guest automatically entering HLT state during idle periods, "
           "resulting in CPU wake-up time consumption.";
}

std::string HaltPollTuner::advice() const
{
#if defined(__aarch64__)
    return "Enable Haltpoll configuration. Keep the vCPU in a polling state for a period of time when idle, "
           "instead of immediately entering HLT, which can effectively reduce the number of CPU sleeps "
           "and decrease the waiting time of interruption.";
#elif defined(__x86_64__)
    return "Enable Optimizer configuration. Keep the vCPU in a polling state for a period of time when idle, "
           "instead of immediately entering HLT, which can effectively reduce the number of CPU sleeps "
           "and decrease the waiting time of interruption.";
#endif
}

bool HaltPollTuner::check()
{
    EBPF_LOG_INFO("Checking Haltpoll tunner.");
    isLastCheckSuccess = true;
    if (checkApply()) {
        EBPF_LOG_INFO("Haltpoll configuration has applied.");
        return false;
    }
    findLastInfer();
    if (inferData.empty()) {
        EBPF_LOG_INFO("Haltpoll configuration is not needed.");
        return false;
    }
    uint64_t maxIPIPerSec = 0;
    for (auto it : inferData) {
        if (it.second != 0) {
            maxIPIPerSec = std::max(maxIPIPerSec, it.first.ipiCount / it.second);
        } else {
            EBPF_LOG_WARN("Broken data found. Please check the clock on guest os.");
        }
    }
    haltpoll = decideHaltpoll(maxIPIPerSec);
    if (haltpoll == 0) {
        EBPF_LOG_INFO("Haltpoll configuration is not needed.");
        return false;
    } else {
        EBPF_LOG_INFO("Haltpoll configuration is needed.");
        return true;
    }
}

#if defined(__aarch64__)
void HaltPollTuner::apply()
{
    std::ostringstream cmd;
    cmd << "echo Y > /sys/module/cpuidle_haltpoll/parameters/force && "
        << "echo " << haltpoll << " > /sys/module/haltpoll/parameters/guest_halt_poll_ns && "
        << "echo N > /sys/module/haltpoll/parameters/guest_halt_poll_allow_shrink";

    auto hostName = utils::getVmName();
    if (hostName.empty()) {
        EBPF_LOG_ERROR("Failed to set haltpoll configuration, due to empty guest name.");
        std::cout << "Failed to set haltpoll configuration, due to empty guest name." << std::endl;
        return;
    }
    CmdExecutor executor(hostName);
    auto result = executor.runCommand(cmd.str());
    if (result.first) {
        EBPF_LOG_INFO("Enable Haltpoll configuration.");
        std::cout << "Haltpoll configuration enabled." << std::endl;
    } else {
        EBPF_LOG_WARN("Failed to set haltpoll configuration, due to : " + result.second);
        std::cout << "Failed to set haltpoll configuration, due to : " << result.second << std::endl;
    }
}
#elif defined(__x86_64__)
void HaltPollTuner::apply()
{
    auto hostName = utils::getVmName();
    if (hostName.empty()) {
        EBPF_LOG_ERROR("Failed to set haltpoll configuration, due to empty guest name.");
        return;
    }
    CmdExecutor executor(hostName);
    auto result = executor.runCommand(R"(sed -i '/^GRUB_CMDLINE_LINUX=/ s/\"$/ idle=poll\"/' /etc/default/grub)");
    if (!result.first) {
        EBPF_LOG_WARN("Failed to set Optimizer configuration. due to : " + result.second);
        std::cout << "Failed to set Optimizer configuration. due to : " << result.second << std::endl;
        return;
    }
    result = executor.runCommand("grub2-mkconfig -o /boot/grub2/grub.cfg");
    if (!result.first) {
        EBPF_LOG_WARN("Failed to load Optimizer configuration. due to : " + result.second);
        std::cout << "Failed to load Optimizer configuration. due to : " + result.second << std::endl;
        auto undoResult = executor.runCommand("sed -i '/^GRUB_CMDLINE_LINUX=/ s/ idle=poll//' /etc/default/grub");
        if (!undoResult.first) {
            EBPF_LOG_ERROR("Failed to load Optimizer, failed to undo the modifications. " + undoResult.second +
                           "This error usually does not occur. Please check whether the format of the "
                           "configuration file /etc/default/grub is correct and remove all 'idle' fields from it.");
        }
        return;
    }
    EBPF_LOG_INFO("Enable Optimizer configuration.");
    std::cout << "Optimizer configuration enabled. It takes effect after restarting the virtual machine." << std::endl;
}
#endif

#if defined(__aarch64__)
bool HaltPollTuner::checkApply()
{
    static const char *cmd = "cat /sys/module/cpuidle_haltpoll/parameters/force";
    static const char *flag = "Y";
    auto hostName = utils::getVmName();
    if (hostName.empty()) {
        isLastCheckSuccess = false;
        return true;
    }
    CmdExecutor executor(hostName);
    auto result = executor.runCommand(cmd);
    if (!result.first) {
        isLastCheckSuccess = false;
        return true;
    } else if (result.second.find(flag) != std::string::npos) {
        return true;
    }
    return false;
}
#elif defined(__x86_64__)
bool HaltPollTuner::checkApply()
{
    static const char *cmd = "cat /proc/cmdline";
    static const char *flag = "idle=poll";
    auto hostName = utils::getVmName();
    if (hostName.empty()) {
        isLastCheckSuccess = false;
        return true;
    }
    CmdExecutor executor(hostName);
    auto result = executor.runCommand(cmd);
    if (!result.first) {
        isLastCheckSuccess = false;
        return true;
    } else if (result.second.find(flag) != std::string::npos) {
        return true;
    }
    return false;
}
#endif

IPIData HaltPollTuner::parseIPIData(const std::string &rawJson)
{
    rapidjson::Document doc;
    rapidjson::ParseResult ok = doc.Parse(rawJson.c_str());
    if (!ok) {
        throw ::std::runtime_error("Not json format.");
    }
    if (!doc.HasMember("timestamp")) {
        throw ::std::runtime_error("Does not have 'timestamp'.");
    }
    auto timestamp = static_cast<time_t>(doc["timestamp"].GetUint64());

    if (!doc.HasMember("data_table")) {
        throw ::std::runtime_error("Does not have 'dataTable'.");
    }

    if (!doc["data_table"].HasMember("ipi_interrupt")) {
        throw ::std::runtime_error("Does not have 'IpiInterrupt'.");
    }
    IpiInterrupt ipiInterrupt{};
    if (!ipiInterrupt.fromJson(doc["data_table"]["ipi_interrupt"])) {
        throw ::std::runtime_error("IpiInterrupt format error.");
    }

    return std::make_pair(ipiInterrupt, timestamp);
}

uint64_t HaltPollTuner::decideHaltpoll(uint64_t maxIPIPerSec)
{
    static std::vector<std::pair<unsigned int, unsigned int>> haltpollThreshold = {
        {20000, 2000000000}, {10000, 1000000000}, {8000, 500000000},
        {6000, 200000000},   {4000, 50000000},    {2000, 20000000}};
    for (auto it : haltpollThreshold) {
        if (maxIPIPerSec > it.first)
            return it.second;
    }
    return 0;
}

void HaltPollTuner::findLastInfer()
{
    inferData.clear();
    try {
        const std::filesystem::path sourcePath =
            std::filesystem::path(std::string(DATA_PATH)) / std::string(DATA_FILE_NAME);
        DataFileSnapshot snapshot(sourcePath);
        std::ifstream &file = snapshot.stream();
        std::string rawJson;
        time_t lastTimestamp = 0;
        while (std::getline(file, rawJson)) {
            IPIData ipiData{};
            try {
                ipiData = parseIPIData(rawJson);
            } catch (const std::exception &e) {
                continue;
            }
            if (lastTimestamp != 0 && ipiData.second > lastTimestamp &&
                ipiData.second - lastTimestamp < MAX_ACCEPTED_INTERVAL &&
                ipiData.first.ipiCount > static_cast<uint64_t>(INFER_THRESHOLD * (ipiData.second - lastTimestamp))) {
                // Convert timestamp to duration.
                ipiData.second = (ipiData.second - lastTimestamp);
                inferData.push_back(ipiData);
                // Excessive data is meaningless.
                if (inferData.size() == MAX_DATA_SIZE) {
                    break;
                }
            }
            lastTimestamp = ipiData.second;
        }
    } catch (const std::exception &e) {
        isLastCheckSuccess = false;
        EBPF_LOG_WARN("Unable to open data file: " + std::string(e.what()));
        return;
    }
    std::reverse(inferData.begin(), inferData.end());
}
