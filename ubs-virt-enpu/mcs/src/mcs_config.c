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

#include "mcs_config.h"
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

mcs_config_t g_mcs_config;

#define DECIMAL_BASE 10

#define MCS_CONFIG_KEY_PHYSICAL_NPU "physical-npu-id"
#define MCS_CONFIG_KEY_VIRTUAL_NPU "virtual-npu-id"
#define MCS_CONFIG_KEY_AICORE_QUOTA "aicore-quota"
#define MCS_CONFIG_KEY_MEMORY_QUOTA "memory-quota"
#define MCS_CONFIG_KEY_VNPU_PRIORITY "vnpu_priority"
#define MCS_CONFIG_KEY_PRIORITY_LEGACY "priority" /* deprecated, accepted for old configs */
#define MCS_CONFIG_KEY_SHM_ID "shm-id"
#define MCS_MAX_LINE_LENGTH 256

static int mcs_parse_line(const char *line, char *key, size_t key_size, char *value, size_t value_size)
{
    const char *eq = strchr(line, '=');
    if (eq == NULL) {
        return -1;
    }

    size_t key_len = (size_t)(eq - line);
    if (key_len >= key_size) {
        return -1;
    }
    (void)strncpy(key, line, key_len);
    key[key_len] = '\0';

    const char *val_start = eq + 1;
    size_t val_len = strlen(val_start);
    /* Trim trailing newline or whitespace */
    while (val_len > 0 &&
           (val_start[val_len - 1] == '\n' || val_start[val_len - 1] == '\r' || val_start[val_len - 1] == ' ')) {
        val_len--;
    }
    if (val_len >= value_size) {
        return -1;
    }
    (void)strncpy(value, val_start, val_len);
    value[val_len] = '\0';

    return 0;
}

/* Strict integer parse: rejects empty strings, non-numeric input, trailing
 * garbage, and out-of-int32 values. atoi() cannot be used for validation
 * because it silently returns 0 on malformed input. */
static int mcs_parse_int32(const char *value, int32_t *out)
{
    if (value == NULL || out == NULL || value[0] == '\0') {
        return -1;
    }
    errno = 0;
    char *endptr = NULL;
    long result = strtol(value, &endptr, DECIMAL_BASE);
    if (errno != 0 || endptr == value || *endptr != '\0') {
        return -1;
    }
    if (result < INT32_MIN || result > INT32_MAX) {
        return -1;
    }
    *out = (int32_t)result;
    return 0;
}

int mcs_load_config(const char *file_path, mcs_config_t *cfg)
{
    if (file_path == NULL || cfg == NULL) {
        return -1;
    }

    FILE *fp = fopen(file_path, "r");
    if (fp == NULL) {
        return -1;
    }

    int found_phy_npu = 0;
    int found_vnpu = 0;
    int found_aicore = 0;
    int found_memory = 0;
    int found_shm_id = 0;

    char line[MCS_MAX_LINE_LENGTH];
    while (fgets(line, sizeof(line), fp) != NULL) {
        /* Skip empty lines and comments */
        if (line[0] == '\n' || line[0] == '#' || line[0] == '\0') {
            continue;
        }

        char key[64];
        char value[MCS_SHM_ID_LEN];
        if (mcs_parse_line(line, key, sizeof(key), value, sizeof(value)) != 0) {
            continue;
        }

        if (strcmp(key, MCS_CONFIG_KEY_PHYSICAL_NPU) == 0) {
            /* Strict parse like the quota fields: atoi() silently turns
             * garbage into 0, which would hide a malformed config. */
            int32_t phy = 0;
            if (mcs_parse_int32(value, &phy) != 0 || phy < 0) {
                fprintf(stderr,
                        "[MCS] FATAL: invalid physical-npu-id '%s' (must be an integer >= 0) - refusing to start\n",
                        value);
                (void)fclose(fp);
                (void)memset(cfg, 0, sizeof(*cfg));
                return -1;
            }
            cfg->phy_npu_id = phy;
            found_phy_npu = 1;
        } else if (strcmp(key, MCS_CONFIG_KEY_VIRTUAL_NPU) == 0) {
            /* Strict validation: vnpu_id indexes vnpu_priority[MCS_MAX_VNPU]
             * and the other per-vNPU arrays in shared memory, so an
             * out-of-range or non-numeric value would write out of bounds -
             * refuse to start. */
            int32_t vnpu = 0;
            if (mcs_parse_int32(value, &vnpu) != 0 || vnpu < 0 || vnpu >= MCS_MAX_VNPU) {
                fprintf(stderr,
                        "[MCS] FATAL: invalid virtual-npu-id '%s' (must be an integer in [0, %d)) - "
                        "refusing to start\n",
                        value, MCS_MAX_VNPU);
                (void)fclose(fp);
                (void)memset(cfg, 0, sizeof(*cfg));
                return -1;
            }
            cfg->vnpu_id = vnpu;
            found_vnpu = 1;
        } else if (strcmp(key, MCS_CONFIG_KEY_AICORE_QUOTA) == 0) {
            /* Strict validation: 0, negatives, and non-numeric strings are
             * all fatal - like vcann-rt, a broken quota must not degrade
             * into an uncontrolled pass-through. */
            int32_t quota = 0;
            if (mcs_parse_int32(value, &quota) != 0 || quota < MCS_MIN_AICORE_QUOTA || quota > MCS_MAX_AICORE_QUOTA) {
                fprintf(stderr,
                        "[MCS] FATAL: invalid aicore-quota '%s' (must be an integer in [%d, %d]) - "
                        "refusing to start\n",
                        value, MCS_MIN_AICORE_QUOTA, MCS_MAX_AICORE_QUOTA);
                (void)fclose(fp);
                (void)memset(cfg, 0, sizeof(*cfg));
                return -1;
            }
            cfg->aicore_quota = quota;
            found_aicore = 1;
        } else if (strcmp(key, MCS_CONFIG_KEY_MEMORY_QUOTA) == 0) {
            /* Strict parse: a trailing-garbage or non-numeric quota must not
             * silently clamp (atoi("8192abc") == 8192, atoi("abc") == 0).
             * The > 0 range check below is the single source of truth. */
            int32_t mem = 0;
            if (mcs_parse_int32(value, &mem) != 0) {
                fprintf(stderr, "[MCS] FATAL: invalid memory-quota '%s' (must be an integer > 0) - refusing to start\n",
                        value);
                (void)fclose(fp);
                (void)memset(cfg, 0, sizeof(*cfg));
                return -1;
            }
            cfg->memory_quota = mem;
            found_memory = 1;
        } else if (strcmp(key, MCS_CONFIG_KEY_VNPU_PRIORITY) == 0) {
            /* Optional key: absent means 0 (normal). Present values must be
             * exactly 0 or 1 - negatives, non-numeric strings and >= 2 are
             * fatal, like vcann-rt. */
            int32_t prio = 0;
            if (mcs_parse_int32(value, &prio) != 0 || prio < 0 || prio > 1) {
                fprintf(stderr, "[MCS] FATAL: invalid vnpu_priority '%s' (must be 0 or 1) - refusing to start\n",
                        value);
                (void)fclose(fp);
                (void)memset(cfg, 0, sizeof(*cfg));
                return -1;
            }
            cfg->vnpu_priority = (uint8_t)prio;
        } else if (strcmp(key, MCS_CONFIG_KEY_PRIORITY_LEGACY) == 0) {
            /* Deprecated key: still accepted so existing deployments keep
             * working; if both keys appear, the last one in the file wins.
             * Same strict validation as vnpu_priority. */
            static int legacy_warned = 0;
            int32_t prio = 0;
            if (!legacy_warned) {
                legacy_warned = 1;
                fprintf(stderr, "[MCS] warning: config key 'priority' is deprecated, use 'vnpu_priority' instead\n");
            }
            if (mcs_parse_int32(value, &prio) != 0 || prio < 0 || prio > 1) {
                fprintf(stderr, "[MCS] FATAL: invalid priority '%s' (must be 0 or 1) - refusing to start\n", value);
                (void)fclose(fp);
                (void)memset(cfg, 0, sizeof(*cfg));
                return -1;
            }
            cfg->vnpu_priority = (uint8_t)prio;
        } else if (strcmp(key, MCS_CONFIG_KEY_SHM_ID) == 0) {
            (void)strncpy(cfg->shm_id, value, sizeof(cfg->shm_id) - 1);
            cfg->shm_id[sizeof(cfg->shm_id) - 1] = '\0';
            found_shm_id = 1;
        }
    }
    fclose(fp);

    /* Validate all required fields present. vnpu_priority is NOT required:
     * an absent key means normal priority (0). */
    if (!found_phy_npu || !found_vnpu || !found_aicore || !found_memory || !found_shm_id) {
        (void)memset(cfg, 0, sizeof(*cfg));
        return -1;
    }

    /* Defensive range re-check; the strict parse above already guarantees
     * these, kept as a backstop against future parse-path changes. */
    if (cfg->aicore_quota < MCS_MIN_AICORE_QUOTA || cfg->aicore_quota > MCS_MAX_AICORE_QUOTA) {
        (void)memset(cfg, 0, sizeof(*cfg));
        return -1;
    }

    if (cfg->vnpu_priority > 1) {
        (void)memset(cfg, 0, sizeof(*cfg));
        return -1;
    }

    if (cfg->phy_npu_id < 0) {
        (void)memset(cfg, 0, sizeof(*cfg));
        return -1;
    }

    if (cfg->memory_quota <= 0) {
        (void)memset(cfg, 0, sizeof(*cfg));
        return -1;
    }

    return 0;
}

int32_t mcs_calc_limited_aicore(int32_t physical_count, int32_t quota_percent)
{
    if (physical_count <= 0 || quota_percent <= 0) {
        return 0;
    }
    int32_t result = physical_count * quota_percent / 100;
    /* At least 1 aicore */
    if (result < 1) {
        result = 1;
    }
    return result;
}
