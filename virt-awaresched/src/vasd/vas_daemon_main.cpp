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

#include <pthread.h>
#include <signal.h>
#include <cstdlib>
#include <iostream>

#include "cluster_sched.h"
#include "libvirt_helper.h"
#include "logger.h"
#include "vas_cli_parse.h"
#include "vas_cli_process_ctl.h"
#include "vas_security_manager.h"
#include "vasd_arg_parse.h"
#include "vasd_looper.h"

using namespace vas::common;
using namespace vas::sched;
using namespace vas::cli::framework;
using namespace vas::cli::reg;
using namespace vas::sched::acquire;
using namespace vas::security;

int main(int argc, char *argv[])
{
    constexpr int maxCliArgs = 20;

    try {
        struct sigaction ignoreSigpipe {
        };
        ignoreSigpipe.sa_handler = SIG_IGN;
        sigemptyset(&ignoreSigpipe.sa_mask);
        if (sigaction(SIGPIPE, &ignoreSigpipe, nullptr) != 0) {
            std::cout << "Failed to ignore SIGPIPE." << std::endl;
            exit(EXIT_FAILURE);
        }
        sigset_t exitSignalSet{};
        sigemptyset(&exitSignalSet);
        sigaddset(&exitSignalSet, SIGINT);
        sigaddset(&exitSignalSet, SIGTERM);
        if (pthread_sigmask(SIG_BLOCK, &exitSignalSet, nullptr) != 0) {
            std::cout << "Failed to block daemon exit signals." << std::endl;
            exit(EXIT_FAILURE);
        }

        // Initializing the Log Module
        std::cout << "Start to init log module, path=" << LOGPATH << "/" << LOGFILE << std::endl;
        VasRet ret = Logger::Instance().Init(LOGPATH, LOGFILE, MAX_LOGFILESIZE, MAX_LOGFILE, OutputType::FILE);
        if (isVasRetFail(ret)) {
            std::cout << "Failed to init log module, ret=" << ret << std::endl;
            return static_cast<int>(VAS_ERROR);
        }

        // Init dynamic capabilities switch
        if (VasSecurityManager::GetCapabilities() != VAS_OK || VasSecurityManager::SetInitialCapabilities() != VAS_OK) {
            LOG_ERROR("Init dynamic capabilities switch failed.");
            return VAS_ERROR;
        }

        // parameter verification
        if (argc > maxCliArgs) {
            VasCliParse::PrintWithWordWrap("INFO: Unsupported number of parameters");
            return static_cast<int>(VAS_ERROR);
        }
        if (argv == nullptr) {
            VasCliParse::PrintWithWordWrap("INFO: System input error.");
            return static_cast<int>(VAS_ERROR);
        }

        RegisterServerModuleSDK();
        ret = VasCliProcessCtl::MainExecuteProcess(argc, argv);
        if (isVasRetFail(ret)) {
            LOG_ERROR("Failed to set config before start server, " + formatRetCode(ret));
            return static_cast<int>(VAS_ERROR);
        }
        LOG_INFO("Success to set config before start server");
        // Init args, libvirt connect, cluster info
        if (VasdArgParse::Init() == VAS_ERROR || isVasRetFail(LibvirtHelper::GetInstance().Init()) ||
            isVasRetFail(ClusterSched::GetInstance().InitClusterInfo())) {
            LOG_ERROR("Init failed, please check if param valid.");
            LibvirtHelper::GetInstance().DeInit();
            return VAS_ERROR;
        }

        // Reschedule domains which is already started
        ClusterSched::GetInstance().ReSchedStartedVms();
        // Run loop thread
        VasdLooper::Run();
    } catch (const std::exception &e) {
        LOG_ERROR("The vas daemon met the exception" + std::string(e.what()));
        LibvirtHelper::GetInstance().DeInit();
        return static_cast<int>(VAS_ERROR);
    }
}
