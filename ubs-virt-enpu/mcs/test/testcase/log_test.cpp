/*
 * Copyright (c) Huawei Technologies Co., Ltd. 2026-2026. All rights reserved.
 * ubs-virt-enpu is licensed under Mulan PSL v2.
 * You can use this software according to the terms and conditions of the Mulan PSL v2.
 * You may obtain a copy of Mulan PSL v2 at:
 *          http://license.coscl.org.cn/MulanPSL2
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND,
 * EITHER EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT,
 * MERCHANTABILITY OR FIT FOR A PARTICULAR PURPOSE.
 * See the Mulan PSL v2 for more details.
 */

#include <dirent.h>
#include <fcntl.h>
#include <gtest/gtest.h>
#include <sys/stat.h>
#include <unistd.h>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>

#include "mcs_log.h"

namespace {
std::string LogDir()
{
    return "/tmp/mcs-log-test-" + std::to_string(getpid()) + "/";
}

void CleanupLogDir()
{
    std::string dirPath = LogDir();
    DIR *dir = opendir(dirPath.c_str());
    if (dir != nullptr) {
        while (dirent *entry = readdir(dir)) {
            if (strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0) {
                continue;
            }
            unlink((dirPath + entry->d_name).c_str());
        }
        closedir(dir);
    }
    rmdir(dirPath.c_str());
}

void CheckSnprintfResult(int ret, size_t size)
{
    ASSERT_GE(ret, 0);
    ASSERT_LT(static_cast<size_t>(ret), size);
}

class LogTest : public testing::Test {
protected:
    void SetUp() override
    {
        CleanupLogDir();
        mkdir(LogDir().c_str(), 0755);
        int ret = snprintf(g_log_config.log_dir, sizeof(g_log_config.log_dir), "%s", LogDir().c_str());
        CheckSnprintfResult(ret, sizeof(g_log_config.log_dir));
        ret = snprintf(g_log_config.log_path, sizeof(g_log_config.log_path), "%scurrent.log", LogDir().c_str());
        CheckSnprintfResult(ret, sizeof(g_log_config.log_path));
        g_log_config.log_file_path[0] = '\0';
        g_log_config.max_file_size = LOG_MAX_FILE_SIZE;
        g_log_config.max_backup_count = LOG_MAX_BACKUP_COUNT;
        g_log_config.min_log_level = ENPU_LOG_INFO;
        g_log_config.flush_counter = 0;
        g_log_config.compress_check_counter = 0;
        g_log_config.compress_disabled = true;
    }

    void TearDown() override
    {
        if (started_) {
            log_shutdown();
            started_ = false;
        }
        CleanupLogDir();
        unsetenv("ENPU_LOG_LEVEL");
    }

    void StartLog()
    {
        ASSERT_EQ(log_init(), ENPU_SUCCESS);
        started_ = true;
    }

    void StopLog()
    {
        if (started_) {
            log_shutdown();
            started_ = false;
        }
    }

    bool started_ = false;
};

LogMessage MakeMessage(const char *text)
{
    LogMessage msg{};
    msg.level = ENPU_LOG_INFO;
    msg.line = 100;
    int ret = snprintf(msg.filename, sizeof(msg.filename), "%s", "log_test.cpp");
    CheckSnprintfResult(ret, sizeof(msg.filename));
    ret = snprintf(msg.basename, sizeof(msg.basename), "%s", "log_test.cpp");
    CheckSnprintfResult(ret, sizeof(msg.basename));
    ret = snprintf(msg.message, sizeof(msg.message), "%s", text);
    CheckSnprintfResult(ret, sizeof(msg.message));
    clock_gettime(CLOCK_REALTIME, &msg.timestamp);
    return msg;
}
} // namespace

TEST_F(LogTest, QueueRejectsNullArguments)
{
    LogQueue queue{};
    LogMessage msg = MakeMessage("test");
    EXPECT_EQ(log_queue_init(nullptr), ENPU_FAIL);
    EXPECT_EQ(log_queue_push(nullptr, &msg), ENPU_FAIL);
    EXPECT_EQ(log_queue_pop(nullptr, &msg), ENPU_FAIL);
    log_queue_destroy(nullptr);
    ASSERT_EQ(log_queue_init(&queue), ENPU_SUCCESS);
    EXPECT_EQ(log_queue_push(&queue, nullptr), ENPU_FAIL);
    EXPECT_EQ(log_queue_pop(&queue, nullptr), ENPU_FAIL);
    log_queue_destroy(&queue);
}

TEST_F(LogTest, QueuePreservesMessages)
{
    LogQueue queue{};
    ASSERT_EQ(log_queue_init(&queue), ENPU_SUCCESS);
    LogMessage msg = MakeMessage("hello mcs");
    ASSERT_EQ(log_queue_push(&queue, &msg), ENPU_SUCCESS);
    LogMessage popped{};
    ASSERT_EQ(log_queue_pop(&queue, &popped), ENPU_SUCCESS);
    EXPECT_EQ(popped.level, ENPU_LOG_INFO);
    EXPECT_EQ(popped.line, 100);
    EXPECT_STREQ(popped.filename, "log_test.cpp");
    EXPECT_STREQ(popped.basename, "log_test.cpp");
    EXPECT_STREQ(popped.message, "hello mcs");
    log_queue_destroy(&queue);
}

TEST_F(LogTest, QueueSupportsWrapAround)
{
    LogQueue queue{};
    ASSERT_EQ(log_queue_init(&queue), ENPU_SUCCESS);
    LogMessage msg = MakeMessage("wrap");
    for (size_t i = 0; i < LOG_QUEUE_SIZE - 1; ++i) {
        ASSERT_EQ(log_queue_push(&queue, &msg), ENPU_SUCCESS);
    }
    LogMessage popped{};
    ASSERT_EQ(log_queue_pop(&queue, &popped), ENPU_SUCCESS);
    ASSERT_EQ(log_queue_push(&queue, &msg), ENPU_SUCCESS);
    for (size_t i = 0; i < LOG_QUEUE_SIZE - 1; ++i) {
        ASSERT_EQ(log_queue_pop(&queue, &popped), ENPU_SUCCESS);
        EXPECT_STREQ(popped.message, "wrap");
    }
    log_queue_destroy(&queue);
}

TEST_F(LogTest, QueueRejectsOperationsAfterShutdown)
{
    LogQueue queue{};
    ASSERT_EQ(log_queue_init(&queue), ENPU_SUCCESS);
    log_queue_destroy(&queue);
    LogMessage msg = MakeMessage("closed");
    EXPECT_EQ(log_queue_push(&queue, &msg), ENPU_FAIL);
    EXPECT_EQ(log_queue_pop(&queue, &msg), ENPU_FAIL);
}

TEST_F(LogTest, RecognizesOnlyCurrentProcessLogFiles)
{
    char valid[FILE_PATH_LEN];
    int ret = snprintf(valid, sizeof(valid), "%s%s_%s_%d_20261103120000%s", LogDir().c_str(), MODULE_NAME,
                       SUB_MODULE_NAME, getpid(), LOG_FILE_SUFFIX);
    CheckSnprintfResult(ret, sizeof(valid));
    EXPECT_EQ(is_log_file(valid), ENPU_SUCCESS);
    EXPECT_EQ(is_log_file("short.log"), ENPU_FAIL);
    EXPECT_EQ(is_log_file("/tmp/eNPU_MCS_99999_20261103120000.log"), ENPU_FAIL);
    EXPECT_EQ(is_log_file("/tmp/eNPU_MCS_bad_20261103120000.log"), ENPU_FAIL);
    EXPECT_EQ(is_log_file("/tmp/eNPU_MCS_123_20261103120000.txt"), ENPU_FAIL);
}

TEST_F(LogTest, UpdateLogFileCreatesRegularFile)
{
    ASSERT_EQ(update_log_file(), ENPU_SUCCESS);
    struct stat st = {};
    EXPECT_EQ(stat(g_log_config.log_path, &st), 0);
    EXPECT_TRUE(S_ISREG(st.st_mode));
}

TEST_F(LogTest, CompressesCurrentProcessLogs)
{
    char name[FILE_PATH_LEN];
    int ret = snprintf(name, sizeof(name), "%s_%s_%d_20261103120000%s", MODULE_NAME, SUB_MODULE_NAME, getpid(),
                       LOG_FILE_SUFFIX);
    CheckSnprintfResult(ret, sizeof(name));
    std::string path = LogDir() + name;
    int fd = open(path.c_str(), O_WRONLY | O_CREAT | O_TRUNC, 0640);
    ASSERT_GE(fd, 0);
    ASSERT_EQ(write(fd, "test log\n", 9), 9);
    close(fd);

    EXPECT_EQ(compress_file(), ENPU_SUCCESS);
    DIR *dir = opendir(LogDir().c_str());
    ASSERT_NE(dir, nullptr);
    bool foundZip = false;
    while (dirent *entry = readdir(dir)) {
        if (strstr(entry->d_name, ZIP_EXT) != nullptr) {
            foundZip = true;
        }
    }
    closedir(dir);
    EXPECT_TRUE(foundZip);
    EXPECT_NE(access(path.c_str(), F_OK), 0);
}

TEST_F(LogTest, InitializesWritesAndShutsDown)
{
    StartLog();
    g_log_config.min_log_level = ENPU_LOG_DEBUG;
    LOG_DEBUG("debug message %d", 1);
    LOG_INFO("info message");
    LOG_WARN("warn message");
    LOG_ERROR("error message");
    LOG_FATAL("fatal message");
    usleep(100000);
    struct stat st = {};
    EXPECT_EQ(stat(g_log_config.log_path, &st), 0);
    EXPECT_GT(st.st_size, 0);
    StopLog();
}

TEST_F(LogTest, HonorsLogLevelEnvironment)
{
    setenv("ENPU_LOG_LEVEL", "2", 1);
    StartLog();
    EXPECT_EQ(g_log_config.min_log_level, ENPU_LOG_WARN);
    StopLog();

    setenv("ENPU_LOG_LEVEL", "invalid", 1);
    StartLog();
    EXPECT_EQ(g_log_config.min_log_level, ENPU_LOG_INFO);
}

TEST_F(LogTest, FallsBackToStderrBeforeInitialization)
{
    log_print(ENPU_LOG_INFO, __FILE__, __LINE__, "fallback %s", "message");
    SUCCEED();
}
