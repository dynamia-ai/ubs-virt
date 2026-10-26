/*
 * Copyright (c) Huawei Technologies Co., Ltd. 2025-2026. All rights reserved.
 * enpu-manager is licensed under Mulan PSL v2.
 * You can use this software according to the terms and conditions of the Mulan PSL v2.
 * You may obtain a copy of Mulan PSL v2 at:
 *          http://license.coscl.org.cn/MulanPSL2
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND,
 * EITHER EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT,
 * MERCHANTABILITY OR FIT FOR A PARTICULAR PURPOSE.
 * See the Mulan PSL v2 for more details.
 */

#include "config_manager.h"
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>
#include "log.h"
#include "securec.h"

#define CONFIG_DIR_MODE 0755
#define CONFIG_FILE_MODE 0644
#define MAX_CONFIG_CONTENT_LEN 1024
#define MAX_DIR_PATH_LEN 512

static int create_dir_recursive(const char *path)
{
    char tmp[MAX_DIR_PATH_LEN];
    char *p = NULL;
    size_t len = 0;

    if (path == NULL || strlen(path) >= MAX_DIR_PATH_LEN) {
        return ENPU_INVALID_PARAM;
    }

    if (snprintf_s(tmp, sizeof(tmp), sizeof(tmp) - 1, "%s", path) < 0) {
        return ENPU_FAIL;
    }
    len = strlen(tmp);
    if (tmp[len - 1] == '/') {
        tmp[len - 1] = '\0';
    }

    for (p = tmp + 1; *p; p++) {
        if (*p == '/') {
            *p = '\0';
            if (mkdir(tmp, CONFIG_DIR_MODE) != 0 && errno != EEXIST) {
                return ENPU_FAIL;
            }
            *p = '/';
        }
    }

    if (mkdir(tmp, CONFIG_DIR_MODE) != 0 && errno != EEXIST) {
        return ENPU_FAIL;
    }

    return ENPU_SUCCESS;
}

static int generate_config_content(vnpu_config_t *config, char *content, int max_len)
{
    int written = 0;

    if (config == NULL || content == NULL || max_len <= 0) {
        return ENPU_INVALID_PARAM;
    }

    written = snprintf_s(content, max_len, max_len - 1,
                         "physical-npu-id=%d\n"
                         "virtual-npu-id=%d\n"
                         "aicore-quota=%d\n"
                         "memory-request=%lu\n"
                         "memory-limit=%lu\n"
                         "shm-id=%s\n"
                         "scheduling-policy=%d\n"
                         "priority=%d\n",
                         config->phy_npu_id, config->vnpu_id, config->aicore_quota, config->memory_request,
                         config->memory_limit, config->shm_id, config->scheduling_policy, config->priority);

    if (written < 0) {
        return ENPU_FAIL;
    }

    return ENPU_SUCCESS;
}

static int write_file_content(const char *path, const char *content)
{
    int fd = -1;
    ssize_t bytes_written = 0;
    size_t content_len = 0;

    if (path == NULL || content == NULL) {
        return ENPU_INVALID_PARAM;
    }

    fd = open(path, O_WRONLY | O_CREAT | O_TRUNC, CONFIG_FILE_MODE);
    if (fd < 0) {
        return ENPU_FAIL;
    }

    content_len = strlen(content);
    bytes_written = write(fd, content, content_len);
    close(fd);

    if (bytes_written != (ssize_t)content_len) {
        unlink(path);
        return ENPU_FAIL;
    }

    return ENPU_SUCCESS;
}

config_manager_t *config_manager_create(const char *base_path)
{
    config_manager_t *mgr = NULL;
    size_t len = 0;

    mgr = (config_manager_t *)malloc(sizeof(config_manager_t));
    if (mgr == NULL) {
        return NULL;
    }

    if (memset_s(mgr, sizeof(config_manager_t), 0, sizeof(config_manager_t)) != 0) {
        free(mgr);
        return NULL;
    }

    if (base_path != NULL && strlen(base_path) > 0) {
        if (snprintf_s(mgr->base_path, sizeof(mgr->base_path), sizeof(mgr->base_path) - 1, "%s", base_path) < 0) {
            free(mgr);
            return NULL;
        }
    } else {
        if (snprintf_s(mgr->base_path, sizeof(mgr->base_path), sizeof(mgr->base_path) - 1, "%s", CONFIG_BASE_PATH) <
            0) {
            free(mgr);
            return NULL;
        }
    }

    len = strlen(mgr->base_path);
    if (len > 0 && mgr->base_path[len - 1] != '/') {
        if (len + 1 < sizeof(mgr->base_path)) {
            mgr->base_path[len] = '/';
            mgr->base_path[len + 1] = '\0';
        }
    }

    pthread_mutex_init(&mgr->lock, NULL);

    if (create_dir_recursive(mgr->base_path) != ENPU_SUCCESS) {
        pthread_mutex_destroy(&mgr->lock);
        free(mgr);
        return NULL;
    }

    return mgr;
}

int config_manager_destroy(config_manager_t *mgr)
{
    if (mgr == NULL) {
        return ENPU_INVALID_PARAM;
    }

    pthread_mutex_destroy(&mgr->lock);
    free(mgr);

    return ENPU_SUCCESS;
}

int config_manager_write_config(config_manager_t *mgr, const char *pod_uid, const char *container_name,
                                vnpu_config_t *config)
{
    char dir_path[MAX_DIR_PATH_LEN];
    char file_path[MAX_DIR_PATH_LEN];
    char content[MAX_CONFIG_CONTENT_LEN];
    int ret = ENPU_SUCCESS;

    if (mgr == NULL || pod_uid == NULL || container_name == NULL || config == NULL) {
        return ENPU_INVALID_PARAM;
    }

    pthread_mutex_lock(&mgr->lock);

    if (snprintf_s(dir_path, sizeof(dir_path), sizeof(dir_path) - 1, "%s%s/%s", mgr->base_path, pod_uid,
                   container_name) < 0) {
        pthread_mutex_unlock(&mgr->lock);
        return ENPU_FAIL;
    }

    ret = create_dir_recursive(dir_path);
    if (ret != ENPU_SUCCESS) {
        pthread_mutex_unlock(&mgr->lock);
        return ret;
    }

    ret = generate_config_content(config, content, sizeof(content));
    if (ret != ENPU_SUCCESS) {
        pthread_mutex_unlock(&mgr->lock);
        return ret;
    }

    if (snprintf_s(file_path, sizeof(file_path), sizeof(file_path) - 1, "%s/%s", dir_path, CONFIG_FILE_NAME) < 0) {
        pthread_mutex_unlock(&mgr->lock);
        return ENPU_FAIL;
    }

    ret = write_file_content(file_path, content);
    if (ret != ENPU_SUCCESS) {
        pthread_mutex_unlock(&mgr->lock);
        return ret;
    }

    LOG_INFO("npu_info.config generated: %s (pod=%s, container=%s, vnpu_id=%d)", file_path, pod_uid, container_name,
             config->vnpu_id);

    pthread_mutex_unlock(&mgr->lock);
    return ENPU_SUCCESS;
}

int config_manager_delete_config(config_manager_t *mgr, const char *pod_uid, const char *container_name)
{
    char dir_path[MAX_DIR_PATH_LEN];
    char file_path[MAX_DIR_PATH_LEN];
    int ret = ENPU_SUCCESS;

    if (mgr == NULL || pod_uid == NULL || container_name == NULL) {
        return ENPU_INVALID_PARAM;
    }

    pthread_mutex_lock(&mgr->lock);

    if (snprintf_s(dir_path, sizeof(dir_path), sizeof(dir_path) - 1, "%s%s/%s", mgr->base_path, pod_uid,
                   container_name) < 0) {
        pthread_mutex_unlock(&mgr->lock);
        return ENPU_FAIL;
    }

    if (snprintf_s(file_path, sizeof(file_path), sizeof(file_path) - 1, "%s/%s", dir_path, CONFIG_FILE_NAME) < 0) {
        pthread_mutex_unlock(&mgr->lock);
        return ENPU_FAIL;
    }

    if (unlink(file_path) != 0 && errno != ENOENT) {
        ret = ENPU_FAIL;
    }

    if (rmdir(dir_path) != 0 && errno != ENOENT) {}

    char parent_path[MAX_DIR_PATH_LEN];
    ret = snprintf_s(parent_path, sizeof(parent_path), sizeof(parent_path) - 1, "%s%s", mgr->base_path, pod_uid);
    if (ret < 0) {
        pthread_mutex_unlock(&mgr->lock);
        return ENPU_FAIL;
    }
    if (rmdir(parent_path) != 0 && errno != ENOENT && errno != ENOTEMPTY) {
        // 真正异常（权限等）才记
    }

    pthread_mutex_unlock(&mgr->lock);
    return ret;
}

char *config_manager_get_config_path(config_manager_t *mgr, const char *pod_uid, const char *container_name,
                                     char *path_buf, int buf_len)
{
    if (mgr == NULL || pod_uid == NULL || container_name == NULL || path_buf == NULL || buf_len <= 0) {
        return NULL;
    }

    if (snprintf_s(path_buf, buf_len, buf_len - 1, "%s%s/%s/%s", mgr->base_path, pod_uid, container_name,
                   CONFIG_FILE_NAME) < 0) {
        return NULL;
    }

    return path_buf;
}
