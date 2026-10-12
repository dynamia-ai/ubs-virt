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
#include "socket_io.h"
#include "utils.h"

namespace fs = std::filesystem;
const std::string LOG_PATH = "/var/ubs-opt/log/ubs_optimizer_server.log";
constexpr const char *RUNNING_PATH = "/usr/local/sbin/ubs-optimizer";
constexpr unsigned LOOP_INTERVAL = 1;
constexpr unsigned RECEIVER_BUFFER_SIZE = 10;
constexpr unsigned FLUSH_INTERVAL = 10;
constexpr unsigned PORT_NIN_BOUND = 1024;
constexpr unsigned PORT_MAX_BOUND = 49151;
constexpr unsigned MIN_SAMPLING_INTERVAL_SEC = 1;
constexpr unsigned MAX_SAMPLING_INTERVAL_SEC = 600;
constexpr unsigned LISTEN_QUEUE_SIZE = 5;
constexpr int CLIENT_READ_TIMEOUT_MS = 5000;

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
        if (!socket_io::readExact(clientFd, &data, sizeof(data), CLIENT_READ_TIMEOUT_MS)) {
            EBPF_LOG_WARN("Incomplete or timed out VSOCK data received.");
        } else {
            EBPF_LOG_DEBUG("Received JSON:\n" + data.toJson());
            receiver.save(data.toJson(guestName));
        }
        close(clientFd);
    }
}

void stopHandler(int)
{
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

    EBPF_LOG_INFO("Starting service.");
    const auto intervalValue = utils::getConfigUint("sampling_interval");
    if (!intervalValue.has_value() || intervalValue.value() < MIN_SAMPLING_INTERVAL_SEC ||
        intervalValue.value() > MAX_SAMPLING_INTERVAL_SEC) {
        EBPF_LOG_ERROR("Parse config failed, sampling_interval must be an integer within the range of [" +
                       std::to_string(MIN_SAMPLING_INTERVAL_SEC) + ", " + std::to_string(MAX_SAMPLING_INTERVAL_SEC) +
                       "].");
        unlink(PID_FILE_PATH);
        return;
    }
    interval = static_cast<int>(intervalValue.value());
    const auto vmNameValue = utils::getConfigString("vm_name");
    if (!vmNameValue.has_value()) {
        EBPF_LOG_ERROR("Parse config failed, vm_name is missing or not a string.");
        unlink(PID_FILE_PATH);
        return;
    }
    const std::string &vm_name = vmNameValue.value();
    if (!utils::isValidVmName(vm_name)) {
        EBPF_LOG_ERROR("Invalid vm_name in config, only English letters or numbers are supported.");
        unlink(PID_FILE_PATH);
        return;
    }

    int serverFd = init();
    if (serverFd < 0) {
        unlink(PID_FILE_PATH);
        return;
    }
    const int currentFlags = fcntl(serverFd, F_GETFL, 0);
    if (currentFlags < 0 || fcntl(serverFd, F_SETFL, currentFlags | O_NONBLOCK) < 0) {
        EBPF_LOG_ERROR("Failed to set VSOCK server socket to non-blocking mode.");
        close(serverFd);
        unlink(PID_FILE_PATH);
        return;
    }

    stopFlag = 0;
    qemuRes.store(-1, std::memory_order_release);
    vcpuRes.store(-1, std::memory_order_release);
    auto context = std::make_shared<MutexContext>();
    DataDumpThread dumpThread(data_output_path, context, dumpIntervalSec);
    dumpThread.start();
    Monitor::getInstance().launch();
    VcpubindCollector::getInstance().launch(vm_name, interval, vcpuRes);
    QEMUCollector::getInstance().launch(vm_name, interval, qemuRes);
    mainLoop(serverFd, context);

    if (stopFlag) {
        EBPF_LOG_INFO("Received stop signal.");
    }
    QEMUCollector::getInstance().stop();
    VcpubindCollector::getInstance().stop();
    Monitor::getInstance().stop();
    dumpThread.stop();
    close(serverFd);
    unlink(PID_FILE_PATH);
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
    const auto portValue = utils::getConfigUint("bind_port");
    if (!portValue.has_value()) {
        EBPF_LOG_ERROR("Parse config failed, bind_port is missing or is not an unsigned integer.");
        close(serverFd);
        return -1;
    }
    auto port = portValue.value();
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

    EBPF_LOG_INFO("Server started on port: " + std::to_string(port));
    return serverFd;
}
