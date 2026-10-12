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

#include "ebpf_logger.h"

#include <malloc.h>
#include <syslog.h>
#include <unistd.h>
#include <chrono>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <iomanip>
#include <iostream>
#include <sstream>
#include <thread>
#include <utility>

EbpfLogger::EbpfLogger()
    : running_(false),
      useSyslog_(true),
      useConsole_(false),
      minLogLevel_(LogLevel::INFO),
      maxFileSize_(0)
{
}

EbpfLogger::~EbpfLogger()
{
    cleanup();
}

EbpfLogger &EbpfLogger::getInstance()
{
    static EbpfLogger instance;
    return instance;
}

bool EbpfLogger::init(const std::string &logFile, LogLevel level, bool useSyslog, bool useConsole, size_t maxFileSize)
{
    logFile_ = logFile;
    minLogLevel_ = level;
    useSyslog_ = useSyslog;
    useConsole_ = useConsole;
    maxFileSize_ = maxFileSize;

    fs::path filePath(logFile_);
    logDir_ = filePath.parent_path().string();
    logBaseName_ = filePath.stem().string();

    fs::path pathObj(logFile_);

    try {
        mkdirWithPermission(pathObj.parent_path(),
                            fs::perms::owner_all | fs::perms::group_read | fs::perms::group_exec);

        logFileStream_.open(logFile_, std::ios::app);
        if (!logFileStream_.is_open()) {
            throw ::std::runtime_error("Failed to open log file:" + logFile_);
        }
        fs::permissions(logFile_, fs::perms::owner_read | fs::perms::owner_write | fs::perms::group_read);
        if (useSyslog_) {
            openlog("EbpfCollector", LOG_PID | LOG_CONS, LOG_USER);
        }
    } catch (const std::exception &e) {
        std::cerr << "Failed to initialize log." << std::endl;
        return false;
    }
    running_ = true;
    logWorkerThread_ = std::thread(&EbpfLogger::logThread, this);
    return true;
}

void EbpfLogger::cleanup()
{
    {
        std::lock_guard<std::mutex> lock(mutex_);
        if (!running_ && !logWorkerThread_.joinable()) {
            return;
        }
        running_ = false;
    }
    condVar_.notify_all();
    if (logWorkerThread_.joinable()) {
        logWorkerThread_.join();
    }
    if (logFileStream_.is_open()) {
        logFileStream_.close();
    }
    if (useSyslog_) {
        closelog();
    }
}

void EbpfLogger::logMessage(LogLevel level, const std::string &message, const char *file, const char *function,
                            int line)
{
    if (level < minLogLevel_) {
        return;
    }
    std::ostringstream oss;
    oss << "[" << getTimestamp(logFilenameTimestampFormat) << "]"
        << "[" << levelToString(level) << "]"
        << "[" << getpid() << "]"
        << "[" << getThreadId() << "]"
        << "[" << fs::path(file).filename().string() << ":" << function << ":" << line << "] " << message;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        if (!running_) {
            return;
        }
        logQueue_.push(oss.str());
    }

    condVar_.notify_one();
}

void EbpfLogger::logThread()
{
    bool shouldStop = false;
    while (!shouldStop) {
        std::unique_lock<std::mutex> lock(mutex_);
        condVar_.wait(lock, [this] { return !logQueue_.empty() || !running_; });
        shouldStop = logQueue_.empty() && !running_;
        if (shouldStop) {
            continue;
        }
        std::string entry = std::move(logQueue_.front());
        logQueue_.pop();
        lock.unlock();

        if (logFileStream_.is_open()) {
            logFileStream_ << entry << std::endl;
            logFileStream_.flush();
        }

        if (useSyslog_) {
            syslog(LOG_INFO, "%s", entry.c_str());
        }

        if (useConsole_) {
            std::cout << entry << std::endl;
            fflush(stdout);
        }

        dumpLogFile();
    }
}

void EbpfLogger::dumpLogFile()
{
    if (!logFileStream_.is_open()) {
        return;
    }

    logFileStream_.flush();
    logFileStream_.seekp(0, std::ios::end);
    auto size = logFileStream_.tellp();
    if (size < 0 || static_cast<size_t>(size) < maxFileSize_) {
        return;
    }

    logFileStream_.close();
    std::string dumpFileName = logDir_ + "/" + logBaseName_ + "_" + getTimestamp(logDumpTimestampFormat) + ".log";
    fs::rename(logFile_, dumpFileName);
    fs::permissions(dumpFileName, fs::perms::owner_read | fs::perms::group_read);

    logFileStream_.open(logFile_, std::ios::trunc);
    fs::permissions(logFile_, fs::perms::owner_read | fs::perms::owner_write | fs::perms::group_read);
    archiveLog();
}

std::string EbpfLogger::getTimestamp(const char *format)
{
    auto now = std::chrono::system_clock::now();
    std::time_t timeT = std::chrono::system_clock::to_time_t(now);
    auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(now.time_since_epoch()) % 1000;
    std::tm localTime{};
#ifdef _WIN32
    localtime_s(&localTime, &timeT);
#else
    localtime_r(&timeT, &localTime);
#endif
    std::ostringstream oss;
    oss << std::put_time(&localTime, format);
    if (std::strcmp(format, logFilenameTimestampFormat) == 0) {
        oss << '.' << std::setfill('0') << std::setw(3) << ms.count() << " +0800"; // 3 digits for milliseconds
    }
    return oss.str();
}

std::string EbpfLogger::getThreadId()
{
    std::ostringstream oss;
    oss << std::this_thread::get_id();
    return oss.str();
}

constexpr std::string_view EbpfLogger::levelToString(LogLevel level)
{
    switch (level) {
        case LogLevel::DEBUG:
            return "DEBUG";
        case LogLevel::INFO:
            return "INFO";
        case LogLevel::WARNING:
            return "WARNING";
        case LogLevel::ERROR:
            return "ERROR";
        case LogLevel::FATAL:
            return "FATAL";
        default:
            return "UNKNOWN";
    }
}

void EbpfLogger::archiveLog()
{
    try {
        std::vector<std::string> dumpFiles;
        for (const auto &entry : fs::directory_iterator(logDir_)) {
            if (entry.is_regular_file() &&
                entry.path().filename().string().find(logBaseName_ + "_") != std::string::npos) {
                dumpFiles.push_back(entry.path().filename().string());
            }
        }

        if (dumpFiles.size() < MAX_LOG_FILES) {
            return;
        }

        // Whitelist control
        std::string archiveLogName = logDir_ + "/" + logBaseName_ + getTimestamp(logDumpTimestampFormat) + ".tar.gz";

        std::string cmd = "tar -czf " + archiveLogName + " -C " + logDir_ + " ";
        for (const auto &file : dumpFiles) {
            cmd += file + " ";
        }

        int ret = std::system(cmd.c_str());
        malloc_trim(0);
        if (ret == 0) {
            fs::permissions(archiveLogName, fs::perms::owner_read | fs::perms::group_read);
            for (const auto &file : dumpFiles) {
                fs::remove(logDir_ + "/" + file);
            }
        } else {
            std::cerr << "Failed to archive logs to: " << archiveLogName
                      << ", system() returned: " << std::to_string(ret) << std::endl;
        }
    } catch (const std::exception &e) {
        std::cerr << "Exception in archiveOldLogs: " << std::string(e.what()) << std::endl;
    }
}

void EbpfLogger::mkdirWithPermission(const fs::path &path, fs::perms permission)
{
    if (!path.empty() && !fs::exists(path)) {
        fs::path curDir;
        for (const auto &part : path) {
            curDir /= part;
            if (!fs::exists(curDir)) {
                fs::create_directories(curDir);
                fs::permissions(curDir, permission);
            }
        }
    }
}
