/*
 * Copyright (c) Huawei Technologies Co., Ltd. 2025-2026. All rights reserved.
 * enpu-manager is licensed under Mulan PSL v2.
 */

#include "enpu_config.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include "log.h"
#include "securec.h"

static void copy_config_str(char *dest, size_t dest_size, const char *src)
{
    if (src == NULL) {
        LOG_ERROR("[CONFIG] copy_config_str: src is NULL");
        return;
    }

    errno_t ret = strncpy_s(dest, dest_size, src, strnlen(src, dest_size - 1));
    if (ret != 0) {
        LOG_ERROR("[CONFIG] strncpy_s failed, ret=%d, dest_size=%zu", ret, dest_size);
    }
}

static void init_defaults(enpu_config_t *config)
{
    copy_config_str(config->socket_path, MAX_PATH_LEN, ENPU_CONFIG_DEFAULT_SOCKET_PATH);

    copy_config_str(config->config_dir, MAX_PATH_LEN, "/etc/enpu");
    copy_config_str(config->state_dir, MAX_PATH_LEN, "/var/lib/enpu-manager");

    copy_config_str(config->log_level, MAX_NAME_LEN, "info");
    copy_config_str(config->log_dir, MAX_PATH_LEN, "/var/log/enpu-manager");
    config->log_max_size = ENPU_CONFIG_DEFAULT_LOG_MAX_SIZE;
    config->log_max_backups = ENPU_CONFIG_DEFAULT_LOG_MAX_BACKUPS;
    config->log_max_age = ENPU_CONFIG_DEFAULT_LOG_MAX_AGE;
    config->log_console = ENPU_CONFIG_DEFAULT_LOG_CONSOLE;

    /* oversub_ratio 默认全 0（calloc 已清零，显式声明意图） */
    for (int i = 0; i < MAX_NPU_PER_NODE; i++) {
        config->oversub_ratio[i] = 0.0;
    }
    config->swap_pre_watermark = ENPU_CONFIG_DEFAULT_SWAP_PRE_WATERMARK;

    errno_t ret = strncpy_s(config->share_strategy, MAX_NAME_LEN, ENPU_CONFIG_DEFAULT_SHARE_STRATEGY,
                            strnlen(ENPU_CONFIG_DEFAULT_SHARE_STRATEGY, MAX_NAME_LEN - 1));
    if (ret != 0) {
        LOG_ERROR("[CONFIG] strncpy_s share_strategy failed, ret=%d", ret);
    }
}

/* 解析 oversub-ratio CSV 字符串，按 phy_id 顺序填入 config->oversub_ratio[] */
static void parse_oversub_ratio_csv(const char *val, enpu_config_t *config)
{
    if (val == NULL || config == NULL) {
        return;
    }

    /* 工作副本（strtok_r 会修改原串） */
    char buf[512];
    errno_t ret = strncpy_s(buf, sizeof(buf), val, strnlen(val, sizeof(buf) - 1));
    if (ret != 0) {
        LOG_ERROR("[CONFIG] strncpy_s buf failed, ret=%d", ret);
        return;
    }

    /* 第一遍：按 ',' 分割，记录所有 token */
    double values[MAX_NPU_PER_NODE];
    int count = 0;
    char *save = NULL;
    char *tok = strtok_r(buf, ",", &save);
    while (tok != NULL && count < MAX_NPU_PER_NODE) {
        /* trim 前导空格 */
        while (*tok == ' ' || *tok == '\t') {
            tok++;
        }
        values[count++] = strtod(tok, NULL);
        tok = strtok_r(NULL, ",", &save);
    }

    if (count == 0) {
        return;
    }

    /* 所有 die 都填这个值 */
    if (count == 1) {
        for (int i = 0; i < MAX_NPU_PER_NODE; i++) {
            config->oversub_ratio[i] = values[0];
        }
        return;
    }

    /* 按下标填，剩余 die 保持 0 */
    for (int i = 0; i < count && i < MAX_NPU_PER_NODE; i++) {
        config->oversub_ratio[i] = values[i];
    }
}

static void parse_key_value(const char *key, const char *val, enpu_config_t *config)
{
    if (strcmp(key, "socket_path") == 0) {
        copy_config_str(config->socket_path, MAX_PATH_LEN, val);
    } else if (strcmp(key, "config_dir") == 0) {
        copy_config_str(config->config_dir, MAX_PATH_LEN, val);
    } else if (strcmp(key, "state_dir") == 0) {
        copy_config_str(config->state_dir, MAX_PATH_LEN, val);
    } else if (strcmp(key, "log_level") == 0) {
        copy_config_str(config->log_level, MAX_NAME_LEN, val);
    } else if (strcmp(key, "log_dir") == 0) {
        copy_config_str(config->log_dir, MAX_PATH_LEN, val);
    } else if (strcmp(key, "log_max_size") == 0) {
        config->log_max_size = atoi(val);
    } else if (strcmp(key, "log_max_backups") == 0) {
        config->log_max_backups = atoi(val);
    } else if (strcmp(key, "log_max_age") == 0) {
        config->log_max_age = atoi(val);
    } else if (strcmp(key, "log_console") == 0) {
        config->log_console = atoi(val);
    } else if (strcmp(key, "oversub-ratio") == 0) {
        parse_oversub_ratio_csv(val, config);
    } else if (strcmp(key, "swap-pre-watermark") == 0) {
        config->swap_pre_watermark = atoi(val);
    } else if (strcmp(key, "share-strategy") == 0) {
        copy_config_str(config->share_strategy, MAX_NAME_LEN, val);
    }
}

/* 去除字符串首尾空白（空格、制表符、换行符） */
static void trim_whitespace(char *str, size_t buf_size)
{
    char *start = str;
    while (*start == ' ' || *start == '\t' || *start == '\n' || *start == '\r') {
        start++;
    }

    char *end = start + strlen(start);
    while (end > start && (end[-1] == ' ' || end[-1] == '\t' || end[-1] == '\n' || end[-1] == '\r')) {
        end--;
    }

    if (start != str) {
        size_t copy_len = (size_t)(end - start) + 1;
        if (memmove_s(str, buf_size, start, copy_len) != 0) {
            LOG_ERROR("[CONFIG] trim_whitespace: memmove_s failed");
        }
    } else {
        *end = '\0';
    }
}

static void load_from_file(const char *path, enpu_config_t *config)
{
    if (path == NULL) {
        path = ENPU_CONFIG_DEFAULT_PATH;
    }

    FILE *fp = fopen(path, "r");
    if (fp == NULL) {
        return;
    }

    char line[256];
    while (fgets(line, sizeof(line), fp) != NULL) {
        char *eq = strchr(line, '=');
        if (eq == NULL) {
            continue;
        }

        *eq = '\0';
        char *val = eq + 1;

        /* 剥内联注释：'#' 及其后的内容不参与解析 */
        char *comment = strchr(val, '#');
        if (comment != NULL) {
            *comment = '\0';
        }

        trim_whitespace(line, sizeof(line));
        trim_whitespace(val, sizeof(line) - (size_t)(val - line));

        parse_key_value(line, val, config);
    }

    if (ferror(fp) != 0) {
        LOG_ERROR("[CONFIG] Failed to read config file: %s", path);
    }

    if (fclose(fp) != 0) {
        LOG_ERROR("[CONFIG] Failed to close config file: %s", path);
    }
}

static void load_from_env(enpu_config_t *config)
{
    char *val = getenv("ENPU_SOCKET_PATH");
    if (val) {
        copy_config_str(config->socket_path, MAX_PATH_LEN, val);
    }

    val = getenv("ENPU_CONFIG_DIR");
    if (val) {
        copy_config_str(config->config_dir, MAX_PATH_LEN, val);
    }

    val = getenv("ENPU_STATE_DIR");
    if (val) {
        copy_config_str(config->state_dir, MAX_PATH_LEN, val);
    }

    val = getenv("ENPU_LOG_LEVEL");
    if (val) {
        copy_config_str(config->log_level, MAX_NAME_LEN, val);
    }

    val = getenv("ENPU_LOG_DIR");
    if (val) {
        copy_config_str(config->log_dir, MAX_PATH_LEN, val);
    }

    val = getenv("ENPU_LOG_MAX_SIZE");
    if (val) {
        config->log_max_size = atoi(val);
    }

    val = getenv("ENPU_LOG_MAX_BACKUPS");
    if (val) {
        config->log_max_backups = atoi(val);
    }

    val = getenv("ENPU_LOG_MAX_AGE");
    if (val) {
        config->log_max_age = atoi(val);
    }

    val = getenv("ENPU_LOG_CONSOLE");
    if (val) {
        config->log_console = atoi(val);
    }

    val = getenv("ENPU_OVERSUB_RATIO");
    if (val) {
        parse_oversub_ratio_csv(val, config);
    }

    val = getenv("ENPU_SWAP_PRE_WATERMARK");
    if (val) {
        config->swap_pre_watermark = atoi(val);
    }

    val = getenv("ENPU_SHARE_STRATEGY");
    if (val) {
        copy_config_str(config->share_strategy, MAX_NAME_LEN, val);
    }
}

enpu_config_t *enpu_config_load(const char *config_path)
{
    enpu_config_t *config = (enpu_config_t *)calloc(1, sizeof(enpu_config_t));
    if (config == NULL) {
        return NULL;
    }

    init_defaults(config);
    load_from_file(config_path, config);
    load_from_env(config);

    return config;
}

int enpu_config_destroy(enpu_config_t *config)
{
    if (config == NULL) {
        return ENPU_INVALID_PARAM;
    }

    free(config);
    return ENPU_SUCCESS;
}
