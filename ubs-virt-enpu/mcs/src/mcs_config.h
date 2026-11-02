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

#ifndef MCS_CONFIG_H
#define MCS_CONFIG_H

#include <stdint.h>

#if defined(__cplusplus)
extern "C" {
#endif

#define MCS_SHM_ID_LEN 128
#define MCS_MAX_AICORE_QUOTA 100
#define MCS_MIN_AICORE_QUOTA 1
/* vNPU id bounds: ids index the per-vNPU arrays in the shared state, so the
 * valid range is [0, MCS_MAX_VNPU). */
#define MCS_MAX_VNPU 100
#define MCS_CONFIG_PATH "/etc/enpu/mcs/mcs_config.config"

typedef struct {
    int32_t phy_npu_id;
    int32_t vnpu_id;
    int32_t aicore_quota;  /* percentage 1-100 */
    int32_t memory_quota;  /* in MB */
    uint8_t vnpu_priority; /* 0=normal, 1=high */
    char shm_id[MCS_SHM_ID_LEN];
} mcs_config_t;

extern mcs_config_t g_mcs_config;

int mcs_load_config(const char *file_path, mcs_config_t *cfg);

/* Calculate limited aicore count: physical_count * quota_percent / 100 */
int32_t mcs_calc_limited_aicore(int32_t physical_count, int32_t quota_percent);

#if defined(__cplusplus)
}
#endif

#endif /* MCS_CONFIG_H */
