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

#include "collector_manager.h"

#include <unistd.h>
#include <chrono>
#include <csignal>
#include <filesystem>
#include <fstream>
#include <string>

#include "rapidjson/document.h"
#include "rapidjson/error/en.h"

#include "collector/ipi_collector.h"
#include "collector/numa_collector.h"
#include "collector/receivers/ebpf_receiver.h"
#include "collector/sched_collector.h"
#include "log/ebpf_logger.h"
#include "log/ebpf_logger_macros.h"
#include "utils.h"

namespace fs = std::filesystem;
constexpr const char *CONFIG_PATH = "/usr/local/sbin/ubs-optimizer/config.json";
constexpr const char *RUNNING_PATH = "/usr/local/sbin/ubs-optimizer";
constexpr const char *LOG_PATH = "/var/ubs-opt/log/ubs_optimizer_client.log";
constexpr unsigned int WAITING_SIGNALS_INTERVAL_SEC = 1; // The waiting interval for receiving incoming signals
constexpr unsigned int MIN_SAMPLING_INTERVAL_SEC = 1;    // Minimum set sampling interval
constexpr unsigned int MAX_SAMPLING_INTERVAL_SEC = 600;  // Maximum set sampling interval

static volatile sig_atomic_t stopFlag{0};
static volatile sig_atomic_t reloadFlag{0};

CollectorManager &CollectorManager::getInstance()
{
    static CollectorManager instance;
    return instance;
}

bool CollectorManager::loadConfig()
{
    EBPF_LOG_INFO("Loading config.");
    std::ifstream ifs(CONFIG_PATH);
    if (!ifs.is_open()) {
        EBPF_LOG_ERROR("Open config file failed: " + std::string(CONFIG_PATH));
        return false;
    }
    std::stringstream buffer;
    buffer << ifs.rdbuf();
    std::string jsonStr = buffer.str();
    ifs.close();

    rapidjson::Document doc;
    rapidjson::ParseResult ok = doc.Parse(jsonStr.c_str());
    if (!ok) {
        EBPF_LOG_ERROR("Parse config file failed, due to: " + std::string(rapidjson::GetParseError_En(ok.Code())) +
                       " (offset: " + std::to_string(ok.Offset()) + ")");
        return false;
    }
    return launchReceiver(doc) && launchCollector(doc);
}

void stopHandler(int)
{
    stopFlag = 1;
}

void reloadHandler(int)
{
    reloadFlag = 1;
}

void CollectorManager::mainLoop()
{
    signal(SIGTERM, stopHandler);
    signal(SIGHUP, reloadHandler);

    auto &logger = EbpfLogger::getInstance();
    if (!logger.init(LOG_PATH, EbpfLogger::LogLevel::INFO, true, true)) {
        unlink(PID_FILE_PATH);
        return;
    }

    // Remove the memory limit for process locking for bpf programs.
    struct rlimit rlim = {.rlim_cur = RLIM_INFINITY, .rlim_max = RLIM_INFINITY};
    if (setrlimit(RLIMIT_MEMLOCK, &rlim)) {
        EBPF_LOG_ERROR("Failed to set RLIMIT_MEMLOCK. Collector exit.");
        unlink(PID_FILE_PATH);
        return;
    }
    if (!loadConfig()) {
        EBPF_LOG_ERROR("Configuration file format error, startup failed.");
        unlink(PID_FILE_PATH);
        return;
    }
    while (!stopFlag) {
        if (reloadFlag) {
            reloadFlag = 0;
            EBPF_LOG_INFO("Received reload signal.");
            if (!loadConfig()) {
                EBPF_LOG_ERROR("Configuration file format error, ebpf collector exited.");
                break;
            }
        }
        std::this_thread::sleep_for(std::chrono::seconds(WAITING_SIGNALS_INTERVAL_SEC));
    }
    if (stopFlag) {
        EBPF_LOG_INFO("Received stop signal.");
    }
    for (auto collector : collectors) {
        if (collector.second->checkRunning()) {
            collector.second->stopCollecting();
        }
    }
    EBPF_LOG_INFO("Collectors terminate.");
    unlink(PID_FILE_PATH);
}

CollectorManager::CollectorManager()
{
    collectors.emplace("ipi_collector", &IPICollector::getInstance());
    collectors.emplace("sched_collector", &SchedCollector::getInstance());
    collectors.emplace("numa_collector", &NumaCollector::getInstance());
}

bool CollectorManager::launchReceiver(const rapidjson::Document &doc)
{
    if (!doc.IsObject() || !doc.HasMember("sampling_interval")) {
        EBPF_LOG_ERROR("Config file format error. 'sampling_interval' not found.");
        return false;
    }
    if (const auto &value = doc["sampling_interval"]; !value.IsUint()) {
        EBPF_LOG_ERROR("Config file format error."
                       "Value of 'sampling_interval' should be integer within the range of [" +
                       std::to_string(MIN_SAMPLING_INTERVAL_SEC) + ", " + std::to_string(MAX_SAMPLING_INTERVAL_SEC) +
                       "]. ");
        return false;
    } else if (auto interval = value.GetUint();
               interval < MIN_SAMPLING_INTERVAL_SEC || interval > MAX_SAMPLING_INTERVAL_SEC) {
        EBPF_LOG_ERROR("Config file format error."
                       "Value of 'sampling_interval' should be integer within the range of [" +
                       std::to_string(MIN_SAMPLING_INTERVAL_SEC) + ", " + std::to_string(MAX_SAMPLING_INTERVAL_SEC) +
                       "]. ");
        return false;
    } else {
        auto &receiver = EBPFReceiver::getInstance();
        receiver.setSamplingInterval(interval);
        receiver.launch();
        return true;
    }
}

bool CollectorManager::launchCollector(const rapidjson::Document &doc)
{
    if (!doc.IsObject() || !doc.HasMember("system")) {
        EBPF_LOG_ERROR("Config file format error. 'system' section not found.");
        return false;
    }
    const rapidjson::Value &collectorObj = doc["system"];
    if (!collectorObj.IsObject()) {
        EBPF_LOG_ERROR("Config file format error. 'system' should be an object.");
        return false;
    }
    for (auto it = collectorObj.MemberBegin(); it != collectorObj.MemberEnd(); ++it) {
        std::string collectorName = it->name.GetString();
        if (collectors.find(collectorName) == collectors.end()) {
            EBPF_LOG_ERROR("Config file format error. "
                           "Collector '" +
                           collectorName + "' not found.");
            return false;
        }
        if (!it->value.IsString()) {
            EBPF_LOG_ERROR("Config file format error. Value of '" + collectorName + "' should be a string.");
            return false;
        }
        std::string flag = it->value.GetString();
        if (flag != "enable" && flag != "disable") {
            EBPF_LOG_ERROR("Config file format error. "
                           "Value of '" +
                           collectorName + "' should be 'enable' or 'disable'.");
            return false;
        }
    }
    for (auto it = collectorObj.MemberBegin(); it != collectorObj.MemberEnd(); ++it) {
        std::string collectorName = it->name.GetString();
        if (auto collector = collectors[collectorName];
            collector->checkRunning() != !strcmp(it->value.GetString(), "enable")) {
            if (collector->checkRunning()) {
                EBPF_LOG_INFO("Stop " + collectorName + ".");
                collector->stopCollecting();
                continue;
            }
            EBPF_LOG_INFO("Start " + collectorName + ".");
            auto ret = collector->launchCollecting();
            if (ret != CollectorStatus::SUCCESS) {
                EBPF_LOG_ERROR("Failed to launch " + collectorName +
                               ", error code: " + std::to_string(static_cast<double>(ret)));
            } else {
                EBPF_LOG_INFO("Successfully to launch " + collectorName + ".");
            }
        }
    }
    return true;
}

void CollectorManager::daemonize()
{
    pid_t pid = utils::forkWithDir(RUNNING_PATH);
    if (pid > 0 || pid == -1) {
        return;
    }
    std::ofstream(PID_FILE_PATH) << getpid();
    fs::permissions(fs::path(PID_FILE_PATH), fs::perms::owner_read);
    CollectorManager::getInstance().mainLoop();
}
