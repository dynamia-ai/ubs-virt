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

#include "vsock_server.h"

#include <atomic>
#include <csignal>
#include <filesystem>
#include <iostream>
#include <thread>

#include <fcntl.h>
#include <unistd.h>

#include <linux/vm_sockets.h>

#include "rapidjson/document.h"

#include "collector/qemu_collector.h"
#include "collector/vcpubind_collector.h"
#include "control/monitor.h"
#include "data_dump_thread.h"
#include "data_receiver.h"
#include "log/ebpf_logger_macros.h"
#include "utils.h"

namespace fs = std::filesystem;
const std::string LOG_PATH = "/var/ubs-opt/log/ubs_optimizer_server.log";
constexpr const char *RUNNING_PATH = "/usr/local/sbin/ubs-optimizer";
constexpr unsigned LOOP_INTERVAL = 1;
constexpr unsigned RECEIVER_BUFFER_SIZE = 10;
constexpr unsigned FLUSH_INTERVAL = 10;
constexpr unsigned PORT_NIN_BOUND = 1024;
constexpr unsigned PORT_MAX_BOUND = 49151;
constexpr unsigned LISTEN_QUEUE_SIZE = 5;

std::atomic<int> qemuRes(-1);
std::atomic<int> vcpuRes(-1);

static volatile sig_atomic_t stopFlag{0};

int VsockServer::interval = 0;

void VsockServer::mainLoop(const int &serverFd, const std::shared_ptr<MutexContext> &context)
{
    const std::string guestName = utils::getVmName();
    while (!stopFlag) {
        // Host data dump
        DataReceiver receiver(context, RECEIVER_BUFFER_SIZE, FLUSH_INTERVAL, data_output_path);
        if (qemuRes.load(std::memory_order_relaxed) != -1 && vcpuRes.load(std::memory_order_relaxed) != -1) {
            std::ostringstream oss;
            std::time_t timestamp = std::chrono::system_clock::to_time_t(std::chrono::system_clock::now());
            oss << "{"
                << "\"timestamp\":" << timestamp << ","
                << "\"guest_name\":\"\","
                << "\"interval\":" << interval << ","
                << "\"data_table\": {"
                << "\"qemu_migration_count\":" << qemuRes << ","
                << "\"host_preempt_vmcore_count\":" << vcpuRes << "}"
                << "}";
            receiver.save(oss.str());
            qemuRes.store(-1, std::memory_order_release);
            vcpuRes.store(-1, std::memory_order_release);
        }

        int clientFd = accept(serverFd, nullptr, nullptr);
        if (clientFd < 0) {
            if (errno == EAGAIN || errno == EWOULDBLOCK) {
                std::this_thread::sleep_for(std::chrono::seconds(LOOP_INTERVAL));
                continue;
            } else if (errno == EINTR) {
                continue;
            } else {
                EBPF_LOG_ERROR("Accept failed, fd closed. errno=" + std::to_string(errno));
                break;
            }
        }

        DataTable data{};
        ssize_t bytes_read = read(clientFd, &data, sizeof(data));
        if (bytes_read != sizeof(data)) {
            EBPF_LOG_WARN("Incomplete data received.");
        } else {
            EBPF_LOG_DEBUG("Received JSON:\n" + data.toJson());
            receiver.save(data.toJson(guestName));
        }
        close(clientFd);
    }
}

void stopHandler(int)
{
    EBPF_LOG_INFO("Received stop signal.");
    stopFlag = 1;
}

void VsockServer::daemonize()
{
    pid_t pid = utils::forkWithDir(RUNNING_PATH);
    if (pid > 0 || pid == -1) {
        return;
    }
    std::ofstream(PID_FILE_PATH) << getpid();
    fs::permissions(fs::path(PID_FILE_PATH), fs::perms::owner_read);
    signal(SIGTERM, stopHandler);
    auto &logger = EbpfLogger::getInstance();
    if (!logger.init(LOG_PATH, EbpfLogger::LogLevel::INFO, true, true)) {
        unlink(PID_FILE_PATH);
        return;
    }

    auto context = std::make_shared<MutexContext>();
    EBPF_LOG_INFO("Starting service.");
    DataDumpThread dumpThread(data_output_path, context, dumpIntervalSec);
    dumpThread.start();
    int serverFd = init();
    if (serverFd < 0) {
        return;
    }
    fcntl(serverFd, F_SETFL, O_NONBLOCK);
    Monitor::getInstance().launch();
    const rapidjson::Value interval_value = utils::getConfigValue("sampling_interval");
    if (interval_value.IsNull()) {
        EBPF_LOG_ERROR("Parse config failed, can not find sampling_interval");
        close(serverFd);
        return;
    }
    interval = interval_value.GetInt();
    const rapidjson::Value vmname_value = utils::getConfigValue("vm_name");
    if (vmname_value.IsNull() || !vmname_value.IsString()) {
        EBPF_LOG_ERROR("Parse config failed, can not find vm_name");
        close(serverFd);
        return;
    }
    std::string vm_name = vmname_value.GetString();
    if (!utils::isValidVmName(vm_name)) {
        EBPF_LOG_ERROR("Invalid vm_name in config, only English letters or numbers are supported.");
        close(serverFd);
        return;
    }
    VcpubindCollector::getInstance().launch(vm_name, interval, vcpuRes);
    QEMUCollector::getInstance().launch(vm_name, interval, qemuRes);
    mainLoop(serverFd, context);
    unlink(PID_FILE_PATH);
    close(serverFd);
}

int VsockServer::init()
{
    int serverFd = socket(AF_VSOCK, SOCK_STREAM, 0);
    if (serverFd < 0) {
        EBPF_LOG_ERROR("Socket creation failed.");
        return -1;
    }

    struct sockaddr_vm addr = {};
    addr.svm_family = AF_VSOCK;
    addr.svm_cid = VMADDR_CID_HOST; // Host CID=2

    // Get the port binding configuration in the configuration file
    const rapidjson::Value port_value = utils::getConfigValue("bind_port");
    if (port_value.IsNull()) {
        EBPF_LOG_ERROR("Parse config failed, server can not find bind_port");
        close(serverFd);
        return -1;
    }

    if (!port_value.IsUint()) {
        EBPF_LOG_ERROR("bind_port is not an positive integer");
        close(serverFd);
        return -1;
    }
    auto port = port_value.GetUint();
    if (port < PORT_NIN_BOUND || port > PORT_MAX_BOUND) {
        EBPF_LOG_ERROR("bind_port must be a positive integer between " + std::to_string(PORT_NIN_BOUND) + " and " +
                       std::to_string(PORT_MAX_BOUND) + ", current value: " + std::to_string(port));
        close(serverFd);
        return -1;
    }
    addr.svm_port = port;
    if (bind(serverFd, reinterpret_cast<struct sockaddr *>(&addr), sizeof(addr)) < 0) {
        EBPF_LOG_ERROR("Bind failed");
        close(serverFd);
        return -1;
    }

    if (listen(serverFd, LISTEN_QUEUE_SIZE) < 0) {
        EBPF_LOG_ERROR("Listen failed.");
        close(serverFd);
        return -1;
    }

    EBPF_LOG_INFO("Server started on port: " + std::to_string(port_value.GetInt()));
    return serverFd;
}
