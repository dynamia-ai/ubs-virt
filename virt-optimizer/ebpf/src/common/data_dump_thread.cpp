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

#include "data_dump_thread.h"

#include <chrono>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <sstream>
#include <utility>

#include "log/ebpf_logger_macros.h"

namespace fs = std::filesystem;

DataDumpThread::DataDumpThread(const std::string &dataFilePath, std::shared_ptr<MutexContext> ctx, int intervalSec)
    : dataPath(dataFilePath),
      context(std::move(ctx)),
      intervalSeconds(intervalSec),
      stopFlag(false)
{
    fs::path filePath(dataFilePath);
    dataDir = filePath.parent_path().string();
    dataBaseName = filePath.stem().string();
}

DataDumpThread::~DataDumpThread()
{
    stop();
}

void DataDumpThread::start()
{
    stopFlag = false;
    monitorThread = std::thread(&DataDumpThread::run, this);
}

void DataDumpThread::stop()
{
    {
        std::lock_guard<std::mutex> lock(mtx);
        stopFlag = true;
    }
    condVar.notify_all();
    if (monitorThread.joinable()) {
        monitorThread.join();
    }
}

void DataDumpThread::run()
{
    std::unique_lock<std::mutex> lock(mtx);

    while (!stopFlag) {
        if (isOversize()) {
            doDumpData();
        }
        condVar.wait_for(lock, std::chrono::seconds(intervalSeconds), [this]() { return stopFlag.load(); });
    }
}

bool DataDumpThread::isOversize()
{
    try {
        return fs::exists(dataPath) && fs::file_size(dataPath) > DUMP_FILE_SIZE_THRESHOLD;
    } catch (const fs::filesystem_error &e) {
        std::cerr << "[Backup] Filesystem error while checking file size: " << e.what() << std::endl;
    } catch (const std::exception &e) {
        std::cerr << "[Backup] Unexpected error while checking file size: " << e.what() << std::endl;
    }
    return false;
}

void DataDumpThread::doDumpData()
{
    if (!context) { // Avoiding null pointer exceptions
        EBPF_LOG_ERROR("Error: pointer is null!");
        return;
    }
    std::lock_guard<std::mutex> lock(context->fileMutex);

    try {
        std::ifstream src(dataPath, std::ios::binary);
        if (!src) {
            std::cerr << "Failed to open source file: " << dataPath << std::endl;
            return;
        }

        const fs::path dumpDataPath = generateDumpFileName();
        std::ofstream dst(dumpDataPath, std::ios::binary | std::ios::trunc);
        if (!dst) {
            std::cerr << "Failed to create backup file: " << dumpDataPath.string() << std::endl;
            return;
        }
        dst << src.rdbuf();
        if (!dst) {
            std::cerr << "Failed to write backup file: " << dumpDataPath.string() << std::endl;
            dst.close();
            fs::remove(dumpDataPath);
            return;
        }
        dst.close();
        EBPF_LOG_INFO("Backup: " + dataPath + " -> " + dumpDataPath.string());
        fs::permissions(dumpDataPath, fs::perms::owner_read);

        std::ofstream clearFile(dataPath, std::ios::trunc);
        if (!clearFile) {
            std::cerr << "Failed to clear source file: " << dataPath << std::endl;
        }
    } catch (const std::exception &ex) {
        std::cerr << "Backup failed: " << ex.what() << std::endl;
    }
}

std::string DataDumpThread::generateDumpFileName()
{
    auto now = std::chrono::system_clock::now();
    std::time_t timeT = std::chrono::system_clock::to_time_t(now);
    std::tm tm{};
#ifdef _WIN32
    localtime_s(&tm, &timeT);
#else
    localtime_r(&timeT, &tm);
#endif
    std::ostringstream fileName;
    fileName << dataBaseName << "_" << std::put_time(&tm, "%Y%m%d_%H%M%S") << ".json";
    if (dataDir.empty()) {
        return fileName.str();
    }
    return (fs::path(dataDir) / fileName.str()).string();
}
