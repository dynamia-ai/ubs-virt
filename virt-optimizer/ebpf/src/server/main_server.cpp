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

#include "command_parser.h"
#include "log/ebpf_logger.h"
#include "utils.h"
#include "vsock_server.h"

constexpr unsigned startTime = 1;
constexpr unsigned stopTime = 3;
const char *CLI_NAME = "ubs-opt-guard";

int main(int argc, char **argv)
{
    CommandParser parser(CLI_NAME);
    parser.registerHandler(
        "start",
        []() -> bool {
            if (utils::checkRunning(PID_FILE_PATH, CLI_NAME)) {
                std::cout << "Start failed. ubs-opt-guard is running." << std::endl;
                return false;
            } else {
                remove(PID_FILE_PATH);
            }
            VsockServer::daemonize();
            std::this_thread::sleep_for(std::chrono::seconds(startTime));
            if (utils::checkRunning(PID_FILE_PATH, CLI_NAME)) {
                std::cout << "ubs-opt-guard started." << std::endl;
            } else {
                std::cout << "ubs-opt-guard start failed." << std::endl;
            }
            return true;
        },
        "Start the guardian of the eBPF collector.");

    parser.registerHandler(
        "stop",
        []() -> bool {
            if (!utils::checkRunning(PID_FILE_PATH, CLI_NAME)) {
                std::cout << "Stopping ubs-opt-guard failed. ubs-opt-guard is not running." << std::endl;
                return false;
            }
            const auto pid = utils::getDaemonPID(PID_FILE_PATH);
            if (!pid.has_value() || kill(pid.value(), SIGTERM) != 0) {
                std::cout << "Stopping ubs-opt-guard failed. Unable to signal guard." << std::endl;
                return false;
            }
            std::cout << "Stopping ubs-opt-guard." << std::endl;
            std::this_thread::sleep_for(std::chrono::seconds(stopTime));
            if (!utils::checkRunning(PID_FILE_PATH, CLI_NAME)) {
                std::cout << "ubs-opt-guard stopped." << std::endl;
            } else {
                std::cout << "Stopping ubs-opt-guard failed." << std::endl;
            }
            return true;
        },
        "Stop the guard.");

    if (!parser.parse(argc, argv)) {
        return 1;
    }
    return 0;
}
