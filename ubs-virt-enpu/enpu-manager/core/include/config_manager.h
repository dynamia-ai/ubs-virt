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

#ifndef __CONFIG_MANAGER_H__
#define __CONFIG_MANAGER_H__

#include <pthread.h>
#include "common.h"

#define CONFIG_BASE_PATH "/etc/enpu/vcann-rt/"
#define CONFIG_FILE_NAME "npu_info.config"

#define OPTION_MEMORY_REQUEST "memory-request"
#define OPTION_MEMORY_LIMIT "memory-limit"

typedef struct vnpu_config {
    int32_t phy_npu_id;
    int32_t vnpu_id;
    int32_t aicore_quota;
    uint64_t memory_request;
    uint64_t memory_limit;
    int32_t scheduling_policy;
    int32_t priority;
    char shm_id[SHM_ID_LEN];
} vnpu_config_t;

typedef struct config_manager {
    char base_path[MAX_PATH_LEN];
    pthread_mutex_t lock;
} config_manager_t;

#if defined(__cplusplus)
extern "C" {
#endif

config_manager_t *config_manager_create(const char *base_path);
int config_manager_destroy(config_manager_t *mgr);

int config_manager_write_config(config_manager_t *mgr, const char *pod_uid, const char *container_name,
                                vnpu_config_t *config);

int config_manager_delete_config(config_manager_t *mgr, const char *pod_uid, const char *container_name);

char *config_manager_get_config_path(config_manager_t *mgr, const char *pod_uid, const char *container_name,
                                     char *path_buf, int buf_len);

#if defined(__cplusplus)
}
#endif

#endif
