/*
 * Copyright (c) Huawei Technologies Co., Ltd. 2025. All rights reserved.
 *
 * VSched is licensed under the Mulan PSL v2.
 * You can use this software according to the terms and conditions of the Mulan PSL v2.
 * You may obtain a copy of Mulan PSL v2 at:
 *      http://license.coscl.org.cn/MulanPSL2
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY OR FIT FOR A PARTICULAR PURPOSE.
 * See the Mulan PSL v2 for more details.
 */
#include "vas_cli_process_ctl.h"

#include <chrono>
#include <condition_variable>
#include <csignal>
#include <cstdlib>
#include <iostream>
#include <mutex>
#include <thread>

#include "args_util.h"
#include "def.h"
#include "vas_cli_parse.h"
#include "vas_cli_res_echo.h"

namespace vas::common {
using namespace vas::cli::framework;

/**
 * @brief Process command with timeout mechanism
 *
 * Parses CLI arguments and sets a timer to handle command execution
 *
 * @param args Command-line arguments
 * @return VasRet Command execution status
 */
VasRet VasCliProcessCtl::MainExecuteProcess(const int &argc, char *argv[])
{
    const std::vector<std::string> args = ArgsUtil::ArgsToVector(argc, argv);
    VasCliParse::GetInstance().PrtHelp(args);
    if (VasCliParse::GetInstance().needPrtHelp) {
        return VAS_ERROR_CMD;
    }
    VasRet ret = VasCliParse::GetInstance().SdkCliParse(args);
    if (ret != VAS_OK) {
        return ret;
    }
    std::mutex timeoutMutex;
    std::condition_variable timeoutCondition;
    bool commandFinished = false;
    std::thread timeoutThread([&timeoutMutex, &timeoutCondition, &commandFinished]() {
        std::unique_lock<std::mutex> lock(timeoutMutex);
        if (timeoutCondition.wait_for(lock, std::chrono::seconds(CLI_TIMEOUT_SECONDS),
                                      [&commandFinished]() { return commandFinished; })) {
            return;
        }
        lock.unlock();
        VasCliParse::PrintWithWordWrap("ERROR: Timeout " + std::to_string(CLI_TIMEOUT_SECONDS) + "s.\n");
        std::_Exit(SIGALRM);
    });
    auto stopTimeout = [&]() {
        {
            std::lock_guard<std::mutex> lock(timeoutMutex);
            commandFinished = true;
        }
        timeoutCondition.notify_one();
        timeoutThread.join();
    };

    try {
        ret = VasCliResEcho::GetInstance().ExecuteCommand();
    } catch (...) {
        stopTimeout();
        throw;
    }
    stopTimeout();
    return ret;
}
} // namespace vas::common
