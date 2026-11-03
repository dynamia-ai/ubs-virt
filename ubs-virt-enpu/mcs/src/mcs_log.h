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

/*
 * MCS log module, ported from the vcann-rt log module
 * (ubs-virt-enpu/vcann-rt/src/include/log.h) with submodule name "MCS".
 * Log level is configured via the ENPU_LOG_LEVEL env var, same as vcann-rt.
 */
#ifndef __MCS_LOG_H__
#define __MCS_LOG_H__

#include <pthread.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>

#if defined(__cplusplus)
extern "C" {
#endif

#define ZIP_EXT ".tar.gz"
#define LOG_FILE_SUFFIX ".log"
#define MODULE_NAME "eNPU"
#define SUB_MODULE_NAME "MCS"
#define FILE_PATH_LEN 256
#define SET_UMASK_FOR_440 0226
#define LOG_FILE_RIGHT 0640

#define LOG_QUEUE_SIZE 256
#define LOG_MSG_MAX_LEN 512
#define LOG_MAX_FILE_SIZE (10 * 1024 * 1024)
#define LOG_MAX_BACKUP_COUNT 10
#define LOG_FLUSH_INTERVAL 10
#define COMPRESS_CHECK_INTERVAL 64
#define SAFE_EXEC_TIMEOUT_SEC 5
#define SAFE_EXEC_POLL_INTERVAL_US 10000
#define LOG_PUSH_WAIT_TIMEOUT_SEC 5
#define LOG_DROP_WARN_LIMIT 10
#define TIMESTAMP_STR_LEN 64
#define TIMESTAMP_FILE_LEN 32
#define PID_PATTERN_LEN 64
#define LOG_LINE_EXTRA_LEN 256
#define EXIT_CODE_EXEC_FAILED 1
#define DECIMAL_BASE 10
#define FILE_OPEN_MODE 0600

typedef enum
{
    ENPU_LOG_FATAL = 0,
    ENPU_LOG_ERROR,
    ENPU_LOG_WARN,
    ENPU_LOG_INFO,
    ENPU_LOG_DEBUG,
} EnpuLogLevel;

typedef struct {
    EnpuLogLevel level;
    char filename[FILE_PATH_LEN];
    char basename[FILE_PATH_LEN];
    int line;
    char message[LOG_MSG_MAX_LEN];
    struct timespec timestamp;
} LogMessage;

typedef struct {
    LogMessage messages[LOG_QUEUE_SIZE];
    size_t head;
    size_t tail;
    pthread_mutex_t mutex;
    pthread_cond_t not_empty;
    pthread_cond_t not_full;
    bool shutdown;
} LogQueue;

typedef struct {
    char log_dir[FILE_PATH_LEN];
    char log_path[FILE_PATH_LEN];
    char log_file_path[FILE_PATH_LEN];
    size_t max_file_size;
    int max_backup_count;
    EnpuLogLevel min_log_level;
    pthread_mutex_t print_mutex;
    pthread_mutex_t compress_mutex;
    LogQueue log_queue;
    pthread_t consumer_thread;
    FILE *log_file;
    int flush_counter;
    int compress_check_counter;
    bool compress_disabled;
} LogConfig;

#define ENPU_SUCCESS 0
#define ENPU_FAIL 1

extern void log_print(EnpuLogLevel level, const char *filename, int line, const char *format, ...);
extern int log_init(void);
extern void log_shutdown(void);
extern LogConfig g_log_config;

extern int log_queue_init(LogQueue *queue);
extern void log_queue_destroy(LogQueue *queue);
extern int log_queue_push(LogQueue *queue, const LogMessage *msg);
extern int log_queue_pop(LogQueue *queue, LogMessage *msg);

extern int compress_file(void);
extern int is_log_file(const char *filename);
extern int update_log_file(void);

#define LOG_DEBUG(msg, ...)                                                    \
    do {                                                                       \
        if (g_log_config.min_log_level >= ENPU_LOG_DEBUG) {                    \
            log_print(ENPU_LOG_DEBUG, __FILE__, __LINE__, msg, ##__VA_ARGS__); \
        }                                                                      \
    } while (false)
#define LOG_INFO(msg, ...)                                                    \
    do {                                                                      \
        if (g_log_config.min_log_level >= ENPU_LOG_INFO) {                    \
            log_print(ENPU_LOG_INFO, __FILE__, __LINE__, msg, ##__VA_ARGS__); \
        }                                                                     \
    } while (false)
#define LOG_WARN(msg, ...)                                                    \
    do {                                                                      \
        if (g_log_config.min_log_level >= ENPU_LOG_WARN) {                    \
            log_print(ENPU_LOG_WARN, __FILE__, __LINE__, msg, ##__VA_ARGS__); \
        }                                                                     \
    } while (false)
#define LOG_ERROR(msg, ...)                                                    \
    do {                                                                       \
        if (g_log_config.min_log_level >= ENPU_LOG_ERROR) {                    \
            log_print(ENPU_LOG_ERROR, __FILE__, __LINE__, msg, ##__VA_ARGS__); \
        }                                                                      \
    } while (false)
#define LOG_FATAL(msg, ...)                                                \
    do {                                                                   \
        log_print(ENPU_LOG_FATAL, __FILE__, __LINE__, msg, ##__VA_ARGS__); \
    } while (false)

#if defined(__cplusplus)
}
#endif

#endif
