/*
 * Copyright (c) Huawei Technologies Co., Ltd. 2025-2026. All rights reserved.
 * enpu-manager is licensed under Mulan PSL v2.
 */

#ifndef __ENPU_CONFIG_H__
#define __ENPU_CONFIG_H__

#include <stdbool.h>
#include <stdint.h>
#include "common.h"

#define ENPU_CONFIG_DEFAULT_PATH "/etc/enpu/enpu-manager.conf"

#define ENPU_CONFIG_DEFAULT_SOCKET_PATH "/run/enpu/enpu-manager.sock"

#define ENPU_CONFIG_DEFAULT_SWAP_PRE_WATERMARK 80

#define ENPU_CONFIG_DEFAULT_LOG_MAX_SIZE 100
#define ENPU_CONFIG_DEFAULT_LOG_MAX_BACKUPS 5
#define ENPU_CONFIG_DEFAULT_LOG_MAX_AGE 30
#define ENPU_CONFIG_DEFAULT_LOG_CONSOLE 0

#define ENPU_CONFIG_DEFAULT_SHARE_STRATEGY "compact"

typedef struct enpu_config {
    char socket_path[MAX_PATH_LEN];

    char config_dir[MAX_PATH_LEN];
    char state_dir[MAX_PATH_LEN];

    char log_level[MAX_NAME_LEN];
    char log_dir[MAX_PATH_LEN];
    int log_max_size;
    int log_max_backups;
    int log_max_age;
    int log_console;

    double oversub_ratio[MAX_NPU_PER_NODE];
    int swap_pre_watermark;

    char share_strategy[MAX_NAME_LEN]; // compact或anti-fragment
} enpu_config_t;

#if defined(__cplusplus)
extern "C" {
#endif

enpu_config_t *enpu_config_load(const char *config_path);
int enpu_config_destroy(enpu_config_t *config);

#if defined(__cplusplus)
}
#endif

#endif
