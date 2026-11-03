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
 * MCS log module, ported from ubs-virt-enpu/vcann-rt/src/log.c.
 * Differences from the reference:
 *   - log dir: /var/log/enpu/mcs/, log file: eNPU_MCS_<pid>_<timestamp>.log
 *   - log lines printed before log_init() fall back to stderr instead of
 *     being dropped, because MCS interception is live before rtSetDevice init
 */
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <libgen.h>
#include <stdarg.h>
#include <stdbool.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <time.h>

#include "mcs_log.h"
#include "securec.h"

#define EXIT_CODE_CMD_NOT_FOUND 127
#define NS_TO_MS_LENGTH 1000000

/* Mirrors the CHECK_*_LOG macros in vcann-rt src/include/common.h */
#define CHECK_RETURN_ERROR_CODE_LOG(err, error_msg, ...)    \
    do {                                                    \
        if ((err) != ENPU_SUCCESS) {                        \
            fprintf(stderr, error_msg "\n", ##__VA_ARGS__); \
            return (err);                                   \
        }                                                   \
    } while (false)

#define CHECK_ERROR_CODE_LOG(err, error_msg, ...)           \
    do {                                                    \
        if ((err) != ENPU_SUCCESS) {                        \
            fprintf(stderr, error_msg "\n", ##__VA_ARGS__); \
        }                                                   \
    } while (false)

#define CHECK_COND_LOG_PRINT(err, error_msg, ...)           \
    do {                                                    \
        if ((err) != ENPU_SUCCESS) {                        \
            fprintf(stderr, error_msg "\n", ##__VA_ARGS__); \
        }                                                   \
    } while (false)

#define CHECK_COND_RETURN_ERROR_CODE_LOG(cond, error_msg, ...) \
    do {                                                       \
        if (cond) {                                            \
            fprintf(stderr, error_msg "\n", ##__VA_ARGS__);    \
            return ENPU_FAIL;                                  \
        }                                                      \
    } while (false)

LogConfig g_log_config = {
    .log_dir = "/var/log/enpu/mcs/",
    .log_path = "mcs.log",
    .max_file_size = LOG_MAX_FILE_SIZE,
    .max_backup_count = LOG_MAX_BACKUP_COUNT,
    .min_log_level = ENPU_LOG_INFO,
    .flush_counter = 0,
    .compress_check_counter = 0,
    .compress_disabled = false,
};

static const char *log_level_str[] = {"FATAL", "ERROR", "WARN", "INFO", "DEBUG"};

static void *log_consumer_thread(void *arg);

static long get_file_size(const char *filepath)
{
    struct stat st;
    if (stat(filepath, &st) < 0) {
        return 0;
    }
    return st.st_size;
}

static int wait_process_timeout(pid_t pid, int timeout_sec)
{
    struct timespec deadline;
    clock_gettime(CLOCK_REALTIME, &deadline);
    deadline.tv_sec += timeout_sec;

    int status;
    pid_t result = 0;
    while (result == 0) {
        result = waitpid(pid, &status, WNOHANG);
        if (result < 0) {
            return ENPU_FAIL;
        }
        if (result == 0) {
            struct timespec now;
            clock_gettime(CLOCK_REALTIME, &now);
            if (now.tv_sec > deadline.tv_sec || (now.tv_sec == deadline.tv_sec && now.tv_nsec >= deadline.tv_nsec)) {
                return ENPU_FAIL;
            }
            usleep(SAFE_EXEC_POLL_INTERVAL_US);
        }
    }

    if (!WIFEXITED(status) || WEXITSTATUS(status) != 0) {
        return ENPU_FAIL;
    }
    return ENPU_SUCCESS;
}

static int safe_exec_timeout(char *const argv[], int timeout_sec)
{
    pid_t pid = fork();
    if (pid < 0) {
        perror("[eNPU] safe_exec: fork failed");
        return ENPU_FAIL;
    }
    if (pid == 0) {
        execvp(argv[0], argv);
        perror("[eNPU] safe_exec: execvp failed");
        _exit(EXIT_CODE_CMD_NOT_FOUND);
    }

    int ret = wait_process_timeout(pid, timeout_sec);
    if (ret != ENPU_SUCCESS) {
        fprintf(stderr, "[eNPU] safe_exec: command '%s' failed or timed out.\n", argv[0]);
    }
    return ret;
}

static int safe_exec(char *const argv[])
{
    return safe_exec_timeout(argv, SAFE_EXEC_TIMEOUT_SEC);
}

static int extract_pid_from_filename(const char *filename, long *pid_from_filename)
{
    char *path_copy = strdup(filename);
    if (!path_copy) {
        return ENPU_FAIL;
    }
    char *base = basename(path_copy);
    if (!base || *base == '\0') {
        free(path_copy);
        return ENPU_FAIL;
    }

    char *temp = strdup(base);
    if (!temp) {
        free(path_copy);
        return ENPU_FAIL;
    }

    /* Log file name: eNPU_MCS_<pid>_<timestamp>.log → 4 tokens split by "_" */
    const int tokens_num = 4;
    char *tokens[tokens_num];
    int token_count = 0;
    const char *delimiter = "_";
    char *saveptr;
    char *token = strtok_r(temp, delimiter, &saveptr);
    while ((token != NULL) && (token_count < tokens_num)) {
        tokens[token_count++] = token;
        token = strtok_r(NULL, delimiter, &saveptr);
    }

    if (token_count < tokens_num) {
        free(temp);
        free(path_copy);
        return ENPU_FAIL;
    }

    char *endptr;
    const int pid_token_index = 2;
    *pid_from_filename = strtol(tokens[pid_token_index], &endptr, DECIMAL_BASE);
    if (*endptr != '\0') {
        fprintf(stderr, "[eNPU] Failed to get pid from log filename. now log filename is %s.\n", g_log_config.log_path);
        free(temp);
        free(path_copy);
        return ENPU_FAIL;
    }

    free(temp);
    free(path_copy);
    return ENPU_SUCCESS;
}

static int is_current_process(const char *filename)
{
    long pid_from_filename;
    int ret = extract_pid_from_filename(filename, &pid_from_filename);
    if (ret != ENPU_SUCCESS) {
        return ret;
    }

    pid_t current_pid = getpid();
    if (pid_from_filename == (long)current_pid) {
        return ENPU_SUCCESS;
    }
    return ENPU_FAIL;
}

int is_log_file(const char *filename)
{
    size_t len = strlen(filename);
    size_t suffix_len = strlen(LOG_FILE_SUFFIX);
    if ((len < suffix_len) || (strcmp(filename + len - suffix_len, LOG_FILE_SUFFIX) != 0)) {
        return ENPU_FAIL;
    }

    return is_current_process(filename);
}

static int check_entry_is_log(const char *dir_path, const char *entry_name)
{
    if (strcmp(entry_name, ".") == 0 || strcmp(entry_name, "..") == 0) {
        return ENPU_FAIL;
    }
    char full_path[FILE_PATH_LEN];
    int ret = snprintf_s(full_path, sizeof(full_path), sizeof(full_path) - 1, "%s%s", dir_path, entry_name);
    if (ret < 0) {
        return ENPU_FAIL;
    }
    struct stat statbuf;
    if (stat(full_path, &statbuf) != 0) {
        return ENPU_FAIL;
    }
    if (S_ISREG(statbuf.st_mode) && (is_log_file(full_path) == ENPU_SUCCESS)) {
        return ENPU_SUCCESS;
    }
    return ENPU_FAIL;
}

static int count_log_files(void)
{
    DIR *dir = opendir(g_log_config.log_dir);
    if (dir == NULL) {
        fprintf(stderr, "[eNPU] Count log files error, failed to open directory %s\n", g_log_config.log_dir);
        return -1;
    }
    int file_count = 0;
    struct dirent *entry;
    while ((entry = readdir(dir)) != NULL) {
        if (check_entry_is_log(g_log_config.log_dir, entry->d_name) == ENPU_SUCCESS) {
            file_count++;
        }
    }

    if (closedir(dir) == -1) {
        return -1;
    }
    return file_count;
}

static int wait_find_process(pid_t pid, const char *list_file)
{
    int ret = wait_process_timeout(pid, SAFE_EXEC_TIMEOUT_SEC);
    if (ret != ENPU_SUCCESS) {
        fprintf(stderr, "[eNPU] Compress files error: find command failed or timed out.\n");
        unlink(list_file);
    }
    return ret;
}

static int find_log_files(const char *list_file, const char *pid_pattern)
{
    int fd = open(list_file, O_WRONLY | O_CREAT | O_TRUNC, FILE_OPEN_MODE);
    if (fd < 0) {
        perror("[eNPU] Compress files error: failed to create list file");
        return ENPU_FAIL;
    }

    pid_t pid = fork();
    if (pid < 0) {
        perror("[eNPU] Compress files error: fork failed for find");
        close(fd);
        unlink(list_file);
        return ENPU_FAIL;
    }
    if (pid == 0) {
        if (dup2(fd, STDOUT_FILENO) < 0) {
            _exit(EXIT_CODE_EXEC_FAILED);
        }
        close(fd);
        char *find_argv[] = {
            "find",        (char *)g_log_config.log_dir,  "-type", "f", "-iname", (char *)pid_pattern, "!",
            "-iwholename", (char *)g_log_config.log_path, NULL};
        execvp("find", find_argv);
        _exit(EXIT_CODE_CMD_NOT_FOUND);
    }
    close(fd);

    return wait_find_process(pid, list_file);
}

static int delete_compressed_sources(const char *list_file)
{
    FILE *lf = fopen(list_file, "r");
    if (!lf) {
        return ENPU_FAIL;
    }
    char fpath[FILE_PATH_LEN];
    while (fgets(fpath, sizeof(fpath), lf) != NULL) {
        size_t flen = strlen(fpath);
        if (flen > 0 && fpath[flen - 1] == '\n') {
            fpath[flen - 1] = '\0';
        }
        if (fpath[0] != '\0') {
            unlink(fpath);
        }
    }
    if (ferror(lf) != 0) {
        fclose(lf);
        return ENPU_FAIL;
    }
    if (fclose(lf) != 0) {
        return ENPU_FAIL;
    }
    return ENPU_SUCCESS;
}

static int build_compress_paths(char *zip_file, char *list_file, char *pid_pattern)
{
    time_t now = time(NULL);
    struct tm tm_info;
    if (localtime_r(&now, &tm_info) == NULL) {
        perror("[eNPU] Compress files error, failed to get current time");
        return ENPU_FAIL;
    }
    char timestamp[TIMESTAMP_FILE_LEN];
    if (strftime(timestamp, sizeof(timestamp), "%Y%m%d%H%M%S", &tm_info) == 0) {
        perror("[eNPU] Compress files error, failed to format timestamp");
        return ENPU_FAIL;
    }

    int ret = snprintf_s(zip_file, FILE_PATH_LEN, FILE_PATH_LEN - 1, "%s%s_%s%s", g_log_config.log_dir, SUB_MODULE_NAME,
                         timestamp, ZIP_EXT);
    CHECK_COND_RETURN_ERROR_CODE_LOG(ret < 0, "Failed to build zip file path.");

    ret =
        snprintf_s(list_file, FILE_PATH_LEN, FILE_PATH_LEN - 1, "%s.tar_list_%d", g_log_config.log_dir, (int)getpid());
    CHECK_COND_RETURN_ERROR_CODE_LOG(ret < 0, "Failed to build list file path.");

    ret = snprintf_s(pid_pattern, PID_PATTERN_LEN, PID_PATTERN_LEN - 1, "*_%d_*.log", (int)getpid());
    CHECK_COND_RETURN_ERROR_CODE_LOG(ret < 0, "Failed to build pid pattern.");

    return ENPU_SUCCESS;
}

int compress_file(void)
{
    char zip_file[FILE_PATH_LEN];
    char list_file[FILE_PATH_LEN];
    char pid_pattern[PID_PATTERN_LEN];

    int ret = build_compress_paths(zip_file, list_file, pid_pattern);
    if (ret != ENPU_SUCCESS) {
        return ret;
    }

    ret = find_log_files(list_file, pid_pattern);
    if (ret != ENPU_SUCCESS) {
        return ret;
    }

    mode_t old_mask = umask(SET_UMASK_FOR_440);
    char *tar_argv[] = {"tar", "-czpf", zip_file, "-T", list_file, NULL};
    int tar_ret = safe_exec(tar_argv);
    umask(old_mask);

    if (tar_ret != ENPU_SUCCESS) {
        fprintf(stderr, "[eNPU] Compress files error: tar command failed.\n");
        unlink(list_file);
        return ENPU_FAIL;
    }

    ret = delete_compressed_sources(list_file);
    unlink(list_file);

    if (ret != ENPU_SUCCESS) {
        fprintf(stderr, "[eNPU] Compress files error: failed to delete source files.\n");
        return ENPU_FAIL;
    }

    printf("[eNPU] Compressed .log files into %s and deleted the original files.\n", g_log_config.log_dir);
    return ENPU_SUCCESS;
}

int update_log_file(void)
{
    time_t now = time(NULL);
    struct tm tm_now;
    localtime_r(&now, &tm_now);
    char time_str[TIMESTAMP_STR_LEN];
    int ret = strftime(time_str, sizeof(time_str), "%Y%m%d%H%M%S", &tm_now);
    CHECK_COND_RETURN_ERROR_CODE_LOG(ret == 0, "Update log file error: failed to get timestamp.");

    char log_path[FILE_PATH_LEN];
    ret = snprintf_s(log_path, sizeof(log_path), sizeof(log_path) - 1, "%s%s_%s_%d_%s%s", g_log_config.log_dir,
                     MODULE_NAME, SUB_MODULE_NAME, getpid(), time_str, LOG_FILE_SUFFIX);
    CHECK_COND_RETURN_ERROR_CODE_LOG(ret < 0, "Failed to get log file name.");
    ret = strncpy_s(g_log_config.log_path, sizeof(g_log_config.log_path), log_path, strlen(log_path));
    CHECK_COND_RETURN_ERROR_CODE_LOG(ret != 0, "Failed to set g_log_config.log_path %s.", log_path);

    if (creat(g_log_config.log_path, LOG_FILE_RIGHT) < 0) {
        perror("[eNPU] update log file error: Create new log file");
        return ENPU_FAIL;
    }
    return ENPU_SUCCESS;
}

static int rotate_log_by_size(void)
{
    long file_size = get_file_size(g_log_config.log_path);
    CHECK_COND_RETURN_ERROR_CODE_LOG(file_size < 0, "File_size is negative.");
    if ((size_t)file_size < g_log_config.max_file_size) {
        return ENPU_SUCCESS;
    }

    int ret = update_log_file();
    CHECK_RETURN_ERROR_CODE_LOG(ret, "Failed to update log file, now log file is %s.", g_log_config.log_path);
    return ENPU_SUCCESS;
}

static bool g_log_initialized = false;
static volatile bool g_log_running = false;
static pthread_mutex_t g_log_init_mutex = PTHREAD_MUTEX_INITIALIZER;

static void parse_level_env(const char *env_name, EnpuLogLevel default_val, EnpuLogLevel *target)
{
    char *env_val = getenv(env_name);
    if (env_val != NULL) {
        char *endptr;
        errno = 0;
        long level_val = strtol(env_val, &endptr, DECIMAL_BASE);
        if (errno != 0 || *endptr != '\0' || level_val < (long)ENPU_LOG_FATAL || level_val > (long)ENPU_LOG_DEBUG) {
            fprintf(stderr, "[eNPU] Log init: invalid %s='%s', using default.\n", env_name, env_val);
            *target = default_val;
        } else {
            *target = (EnpuLogLevel)level_val;
        }
    } else {
        *target = default_val;
    }
}

int log_init(void)
{
    if (g_log_initialized) {
        return ENPU_SUCCESS;
    }

    pthread_mutex_lock(&g_log_init_mutex);
    if (g_log_initialized) {
        pthread_mutex_unlock(&g_log_init_mutex);
        return ENPU_SUCCESS;
    }

    pthread_mutex_init(&g_log_config.print_mutex, NULL);
    pthread_mutex_init(&g_log_config.compress_mutex, NULL);

    printf("[eNPU] dir_path: %s\n", g_log_config.log_dir);

    char *mkdir_argv[] = {"mkdir", "-p", (char *)g_log_config.log_dir, NULL};
    (void)safe_exec(mkdir_argv);

    int ret = update_log_file();
    if (ret != ENPU_SUCCESS) {
        fprintf(stderr, "[eNPU] Failed to update log file, now log file is %s.\n", g_log_config.log_path);
        pthread_mutex_destroy(&g_log_config.print_mutex);
        pthread_mutex_destroy(&g_log_config.compress_mutex);
        pthread_mutex_unlock(&g_log_init_mutex);
        return ENPU_FAIL;
    }

    parse_level_env("ENPU_LOG_LEVEL", ENPU_LOG_INFO, &g_log_config.min_log_level);

    ret = log_queue_init(&g_log_config.log_queue);
    if (ret != ENPU_SUCCESS) {
        fprintf(stderr, "[eNPU] Log init failed: unable to init log queue.\n");
        pthread_mutex_destroy(&g_log_config.print_mutex);
        pthread_mutex_destroy(&g_log_config.compress_mutex);
        pthread_mutex_unlock(&g_log_init_mutex);
        return ENPU_FAIL;
    }

    g_log_running = true;
    ret = pthread_create(&g_log_config.consumer_thread, NULL, log_consumer_thread, NULL);
    if (ret != 0) {
        fprintf(stderr, "[eNPU] Log init failed: unable to create log consumer thread.\n");
        g_log_running = false;
        log_queue_destroy(&g_log_config.log_queue);
        pthread_mutex_destroy(&g_log_config.print_mutex);
        pthread_mutex_destroy(&g_log_config.compress_mutex);
        pthread_mutex_unlock(&g_log_init_mutex);
        return ENPU_FAIL;
    }

    printf("[eNPU] Async logging enabled with queue size %d.\n", LOG_QUEUE_SIZE);
    g_log_initialized = true;
    pthread_mutex_unlock(&g_log_init_mutex);
    return ENPU_SUCCESS;
}

/* Fallback for messages logged before log_init(): print to stderr so that
 * early LD_PRELOAD interception output is not lost. */
static void log_print_fallback(EnpuLogLevel level, const char *filename, int line, const char *format, va_list args)
{
    char message[LOG_MSG_MAX_LEN];
    int ret = vsnprintf_s(message, sizeof(message), sizeof(message) - 1, format, args);
    if (ret < 0) {
        message[0] = '\0';
    }

    const char *bname = strrchr(filename, '/');
    bname = (bname != NULL) ? bname + 1 : filename;

    fprintf(stderr, "[eNPU] [%s] [%s] [%s] [%d:%s:%d] %s\n", log_level_str[level], MODULE_NAME, SUB_MODULE_NAME,
            (int)getpid(), bname, line, message);
}

void log_print(EnpuLogLevel level, const char *filename, int line, const char *format, ...)
{
    if (level > g_log_config.min_log_level) {
        return;
    }

    va_list args;
    va_start(args, format);
    if (!g_log_initialized) {
        log_print_fallback(level, filename, line, format, args);
        va_end(args);
        return;
    }

    LogMessage msg;
    msg.level = level;
    msg.line = line;

    char path_buf[FILE_PATH_LEN];
    int ret = strncpy_s(path_buf, sizeof(path_buf), filename, strlen(filename));
    CHECK_COND_LOG_PRINT(ret, "strncpy_s path_buf failed");
    char *bname = strrchr(path_buf, '/');
    if (bname != NULL) {
        bname = bname + 1;
    } else {
        bname = path_buf;
    }
    ret = strncpy_s(msg.basename, sizeof(msg.basename), bname, strlen(bname));
    CHECK_COND_LOG_PRINT(ret, "strncpy_s msg.basename failed");

    clock_gettime(CLOCK_REALTIME, &msg.timestamp);

    ret = vsnprintf_s(msg.message, sizeof(msg.message), sizeof(msg.message) - 1, format, args);
    va_end(args);
    if (ret < 0) {
        msg.message[0] = '\0';
    }

    if (log_queue_push(&g_log_config.log_queue, &msg) != ENPU_SUCCESS) {
        static int drop_count = 0;
        int current = __atomic_fetch_add(&drop_count, 1, __ATOMIC_RELAXED);
        if (current < LOG_DROP_WARN_LIMIT) {
            fprintf(stderr, "[eNPU] Log queue push failed: log system unavailable (queue full or shut down).\n");
        }
    }
}

int log_queue_init(LogQueue *queue)
{
    if (!queue) {
        return ENPU_FAIL;
    }

    queue->head = 0;
    queue->tail = 0;
    queue->shutdown = false;

    int ret = pthread_mutex_init(&queue->mutex, NULL);
    if (ret != 0) {
        fprintf(stderr, "[eNPU] Failed to init log queue mutex.\n");
        return ENPU_FAIL;
    }

    ret = pthread_cond_init(&queue->not_empty, NULL);
    if (ret != 0) {
        pthread_mutex_destroy(&queue->mutex);
        fprintf(stderr, "[eNPU] Failed to init log queue not_empty condition.\n");
        return ENPU_FAIL;
    }

    ret = pthread_cond_init(&queue->not_full, NULL);
    if (ret != 0) {
        pthread_mutex_destroy(&queue->mutex);
        pthread_cond_destroy(&queue->not_empty);
        fprintf(stderr, "[eNPU] Failed to init log queue not_full condition.\n");
        return ENPU_FAIL;
    }

    return ENPU_SUCCESS;
}

void log_queue_destroy(LogQueue *queue)
{
    if (!queue) {
        return;
    }

    pthread_mutex_lock(&queue->mutex);
    queue->shutdown = true;
    pthread_cond_broadcast(&queue->not_empty);
    pthread_cond_broadcast(&queue->not_full);
    pthread_mutex_unlock(&queue->mutex);
}

int log_queue_pop(LogQueue *queue, LogMessage *msg)
{
    if (!queue || !msg) {
        fprintf(stderr, "[eNPU] Log queue pop failed: queue or message is NULL.\n");
        return ENPU_FAIL;
    }

    pthread_mutex_lock(&queue->mutex);

    while (queue->head == queue->tail && !queue->shutdown) {
        pthread_cond_wait(&queue->not_empty, &queue->mutex);
    }

    if (queue->shutdown && queue->head == queue->tail) {
        pthread_mutex_unlock(&queue->mutex);
        return ENPU_FAIL;
    }

    *msg = queue->messages[queue->head];
    queue->head = (queue->head + 1) % LOG_QUEUE_SIZE;

    pthread_cond_signal(&queue->not_full);
    pthread_mutex_unlock(&queue->mutex);

    return ENPU_SUCCESS;
}

static int log_queue_wait_for_space(LogQueue *queue)
{
    struct timespec wait_deadline;
    clock_gettime(CLOCK_REALTIME, &wait_deadline);
    wait_deadline.tv_sec += LOG_PUSH_WAIT_TIMEOUT_SEC;

    while ((queue->tail + 1) % LOG_QUEUE_SIZE == queue->head && !queue->shutdown) {
        int wait_ret = pthread_cond_timedwait(&queue->not_full, &queue->mutex, &wait_deadline);
        if (wait_ret == ETIMEDOUT) {
            return ENPU_FAIL;
        }
    }

    if (queue->shutdown) {
        return ENPU_FAIL;
    }
    return ENPU_SUCCESS;
}

int log_queue_push(LogQueue *queue, const LogMessage *msg)
{
    if (!queue || !msg) {
        fprintf(stderr, "[eNPU] Log queue push failed: queue or message is NULL.\n");
        return ENPU_FAIL;
    }

    pthread_mutex_lock(&queue->mutex);

    if (queue->shutdown) {
        pthread_mutex_unlock(&queue->mutex);
        return ENPU_FAIL;
    }

    if ((queue->tail + 1) % LOG_QUEUE_SIZE == queue->head) {
        int wait_ret = log_queue_wait_for_space(queue);
        if (wait_ret != ENPU_SUCCESS) {
            pthread_mutex_unlock(&queue->mutex);
            return ENPU_FAIL;
        }
    }

    queue->messages[queue->tail] = *msg;
    queue->tail = (queue->tail + 1) % LOG_QUEUE_SIZE;

    pthread_cond_signal(&queue->not_empty);
    pthread_mutex_unlock(&queue->mutex);

    return ENPU_SUCCESS;
}

static int prepare_log_file(void)
{
    if (!g_log_config.log_file || (strcmp(g_log_config.log_path, g_log_config.log_file_path) != 0)) {
        if (g_log_config.log_file) {
            fclose(g_log_config.log_file);
        }
        int ret = strncpy_s(g_log_config.log_file_path, sizeof(g_log_config.log_file_path), g_log_config.log_path,
                            strlen(g_log_config.log_path));
        if (ret != 0) {
            return ret;
        }
        g_log_config.log_file = fopen(g_log_config.log_path, "a");
        if (!g_log_config.log_file) {
            return ENPU_FAIL;
        }
    }
    return ENPU_SUCCESS;
}

static int write_log_message(const LogMessage *msg, char *time_str, char *log_line)
{
    int ret = rotate_log_by_size();
    if (ret != ENPU_SUCCESS) {
        return ret;
    }

    ret = prepare_log_file();
    if (ret != ENPU_SUCCESS) {
        return ret;
    }

    struct tm tm_now;
    localtime_r(&msg->timestamp.tv_sec, &tm_now);
    char format[TIMESTAMP_STR_LEN];
    if (strftime(format, TIMESTAMP_STR_LEN, "%Y-%m-%d %H:%M:%S", &tm_now) == 0) {
        ret = strcpy_s(time_str, TIMESTAMP_STR_LEN, "TIME_ERROR");
        CHECK_COND_LOG_PRINT(ret, "strcpy_s time_str failed.");
    } else {
        int milliseconds = msg->timestamp.tv_nsec / NS_TO_MS_LENGTH;
        ret = snprintf_s(time_str, TIMESTAMP_STR_LEN, TIMESTAMP_STR_LEN - 1, "%s.%03d", format, milliseconds);
        CHECK_COND_LOG_PRINT(ret < 0, "get timestamp ms failed.");
    }

    ret = snprintf_s(log_line, LOG_MSG_MAX_LEN + LOG_LINE_EXTRA_LEN, LOG_MSG_MAX_LEN + LOG_LINE_EXTRA_LEN - 1,
                     "[%s] [%s] [%s] [%s] [%d:%lu:%s:%d] %s\n", time_str, log_level_str[msg->level], MODULE_NAME,
                     SUB_MODULE_NAME, getpid(), (unsigned long)pthread_self(), msg->basename, msg->line, msg->message);
    if (ret > 0) {
        size_t buf_size = LOG_MSG_MAX_LEN + LOG_LINE_EXTRA_LEN;
        if ((size_t)ret >= buf_size) {
            const char *trunc_marker = "...[TRUNCATED]\n";
            size_t marker_len = strlen(trunc_marker);
            size_t pos = buf_size - marker_len - 1;
            int copy_ret = memcpy_s(log_line + pos, buf_size - pos, trunc_marker, marker_len + 1);
            CHECK_COND_LOG_PRINT(copy_ret, "memcpy_s truncation marker failed");
        }
        fprintf(g_log_config.log_file, "%s", log_line);
        if (msg->level <= ENPU_LOG_INFO) {
            fprintf(stderr, "%s", log_line);
        }
    }

    if (msg->level <= ENPU_LOG_ERROR) {
        fflush(g_log_config.log_file);
        g_log_config.flush_counter = 0;
    } else {
        if (++g_log_config.flush_counter >= LOG_FLUSH_INTERVAL) {
            fflush(g_log_config.log_file);
            g_log_config.flush_counter = 0;
        }
    }

    return ENPU_SUCCESS;
}

static void check_compress_log(void)
{
    if (g_log_config.compress_disabled) {
        return;
    }

    if (++g_log_config.compress_check_counter < COMPRESS_CHECK_INTERVAL) {
        return;
    }
    g_log_config.compress_check_counter = 0;

    pthread_mutex_lock(&g_log_config.compress_mutex);
    int log_file_count = count_log_files();
    if (log_file_count > g_log_config.max_backup_count) {
        int ret = compress_file();
        CHECK_ERROR_CODE_LOG(ret, "Failed to compress log files, log_dir is %s.", g_log_config.log_dir);
    }
    pthread_mutex_unlock(&g_log_config.compress_mutex);
}

void *log_consumer_thread(void *arg)
{
    (void)arg;
    LogMessage msg;
    char time_str[TIMESTAMP_STR_LEN];
    char log_line[LOG_MSG_MAX_LEN + LOG_LINE_EXTRA_LEN];

    do {
        if (log_queue_pop(&g_log_config.log_queue, &msg) != ENPU_SUCCESS) {
            break;
        }

        pthread_mutex_lock(&g_log_config.print_mutex);
        (void)write_log_message(&msg, time_str, log_line);
        /* Flush per message: the process is typically killed with SIGKILL in
         * tests, which bypasses exit-time stdio flush; with the previous
         * flush-every-10-messages scheme a short run below the threshold lost
         * every log record. */
        if (g_log_config.log_file != NULL) {
            fflush(g_log_config.log_file);
        }
        pthread_mutex_unlock(&g_log_config.print_mutex);

        if (g_log_running) {
            check_compress_log();
        }
    } while (g_log_running);

    return NULL;
}

void log_shutdown(void)
{
    if (g_log_config.log_queue.shutdown) {
        return;
    }

    g_log_running = false;
    log_queue_destroy(&g_log_config.log_queue);

    if (g_log_config.consumer_thread != 0) {
        pthread_join(g_log_config.consumer_thread, NULL);
        g_log_config.consumer_thread = 0;
    }

    pthread_mutex_destroy(&g_log_config.log_queue.mutex);
    pthread_cond_destroy(&g_log_config.log_queue.not_empty);
    pthread_cond_destroy(&g_log_config.log_queue.not_full);

    if (g_log_config.log_file) {
        fflush(g_log_config.log_file);
        fclose(g_log_config.log_file);
        g_log_config.log_file = NULL;
    }

    pthread_mutex_destroy(&g_log_config.print_mutex);
    pthread_mutex_destroy(&g_log_config.compress_mutex);

    g_log_initialized = false;
}
