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

#include <unistd.h>
#include <csignal>
#include <iostream>
#include <thread>

#include "collector_manager.h"
#include "command_parser.h"
#include "utils.h"

constexpr unsigned startTime = 1;
constexpr unsigned stopTime = 3;
const char *CLI_NAME = "ubs-opt";

bool handleStartEbpf()
{
    if (utils::checkRunning(PID_FILE_PATH, CLI_NAME)) {
        std::cout << "Failed to start eBPF collector. Collector is already running." << std::endl;
        return false;
    } else {
        remove(PID_FILE_PATH);
    }
    CollectorManager::daemonize();
    std::this_thread::sleep_for(std::chrono::seconds(startTime));
    if (utils::checkRunning(PID_FILE_PATH, CLI_NAME)) {
        std::cout << "eBPF collector started." << std::endl;
    } else {
        std::cout << "Failed to start eBPF collector." << std::endl;
    }
    return true;
}

bool handleStopEbpf()
{
    if (!utils::checkRunning(PID_FILE_PATH, CLI_NAME)) {
        std::cout << "Stop eBPF collector failed. Collector is already stopped." << std::endl;
        return false;
    }
    const auto pid = utils::getDaemonPID(PID_FILE_PATH);
    if (!pid.has_value() || kill(pid.value(), SIGTERM) != 0) {
        std::cout << "Stop eBPF collector failed. Unable to signal collector." << std::endl;
        return false;
    }
    std::cout << "Stopping eBPF collector" << std::endl;
    std::this_thread::sleep_for(std::chrono::seconds(stopTime));
    if (!utils::checkRunning(PID_FILE_PATH, CLI_NAME)) {
        std::cout << "eBPF collector stopped." << std::endl;
    } else {
        std::cout << "Failed to stop eBPF collector." << std::endl;
    }
    return true;
}

bool handleReloadEbpf()
{
    if (!utils::checkRunning(PID_FILE_PATH, CLI_NAME)) {
        std::cout << "Failed to reload config. Collector is not running." << std::endl;
        return false;
    }
    const auto pid = utils::getDaemonPID(PID_FILE_PATH);
    if (!pid.has_value() || kill(pid.value(), SIGHUP) != 0) {
        std::cout << "Failed to reload config. Unable to signal collector." << std::endl;
        return false;
    }
    std::cout << "Reload eBPF collector config successfully." << std::endl;
    return true;
}

void registerCommands(CommandParser &parser)
{
    parser.registerHandler("start_ebpf", handleStartEbpf, "Start the eBPF collector.");
    parser.registerHandler("stop_ebpf", handleStopEbpf, "Stop the eBPF collector.");
    parser.registerHandler("reload_ebpf", handleReloadEbpf, "Reload the config file from the disk.");
}

int main(int argc, char **argv)
{
    CommandParser parser(CLI_NAME);
    registerCommands(parser);
    if (!parser.parse(argc, argv)) {
        return 1;
    }
    return 0;
}
