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

#ifndef __ENPU_DCMI_WRAPPER_H__
#define __ENPU_DCMI_WRAPPER_H__

#include "common.h"

#if defined(__cplusplus)
extern "C" {
#endif

#define DCMI_MAX_PIDS 64
#define DCMI_MAX_CHIP_NUM 32
#define DCMI_INVALID_CARD_ID (-1)
#define DCMI_INVALID_DEVICE_ID (-1)

typedef enum
{
    SOC_VERSION_ASCEND_950 = 0,
    SOC_VERSION_NOT_ASCEND_950 = 1,
} soc_version_t;

typedef struct {
    int card_id;
    int device_id;
    int logic_id;
    char uuid[MAX_UUID_LEN];
    uint64_t total_memory;
    uint32_t total_aicore;
} dcmi_device_info_t;

typedef struct {
    int phy_id;
    int card_id;
    int device_id;
    char die_id[DIE_ID_LEN];
    char shm_id[DIE_ID_LEN];
} dcmi_die_info_t;

typedef struct {
    uint32_t aicore_utilization;
    uint32_t aicpu_utilization;
    uint32_t total_utilization;
} dcmi_utilization_t;

typedef enum
{
    DCMI_METRIC_OK = 0,
    DCMI_METRIC_UNSUPPORTED,
    DCMI_METRIC_UNAVAILABLE,
    DCMI_METRIC_INCONSISTENT,
} dcmi_metric_status_t;

typedef struct {
    unsigned int value;
    dcmi_metric_status_t status;
    int source_result;                /* Raw DCMI result; ENPU_FAIL when no source call was possible. */
    uint64_t sampled_at_monotonic_ns; /* Successful calls only; zero if unavailable. */
} dcmi_metric_t;

typedef struct {
    dcmi_metric_t npu;
    dcmi_metric_t aicore;
    dcmi_metric_t aicpu;
    dcmi_metric_t ctrlcpu;
    dcmi_metric_t cube;
    dcmi_metric_t vector;
} dcmi_resource_metrics_t;

typedef struct {
    uint64_t total_memory;
    uint64_t used_memory;
    uint64_t free_memory;
} dcmi_memory_info_t;

int dcmi_wrapper_init(void);
void dcmi_wrapper_fini(void);
soc_version_t dcmi_wrapper_get_soc_version(void);

int dcmi_get_device_count(int *count);
int dcmi_get_device_list(int *logic_ids, int max_count, int *actual_count);
int dcmi_get_device_info(int logic_id, dcmi_device_info_t *info);

int dcmi_get_card_info(int logic_id, int *card_id, int *device_id);
int dcmi_get_logic_id(int card_id, int device_id, int *logic_id);

int dcmi_get_die_info(int logic_id, dcmi_die_info_t *die_info);
int dcmi_get_shm_id_by_logic_id(int logic_id, char *shm_id, size_t shm_id_size);

int dcmi_get_utilization(int logic_id, dcmi_utilization_t *utilization);
int dcmi_get_utilization_by_card(int card_id, int device_id, dcmi_utilization_t *utilization);

/* logic_id is the software device ID enumerated by the manager's NPU tree,
 * not a hardware chip physical ID. Resolves legacy card/device once; 950 uses
 * logic_id directly. SUCCESS means queries were attempted, not all metrics OK.
 * On failure, all supplied metrics are initialized to unavailable.
 */
int dcmi_query_resource_metrics(int logic_id, dcmi_resource_metrics_t *metrics);

int dcmi_get_memory_info(int logic_id, dcmi_memory_info_t *mem_info);
int dcmi_get_memory_info_by_card(int card_id, int device_id, dcmi_memory_info_t *mem_info);
int dcmi_get_process_memory(int card_id, int device_id, uint64_t *used);

#if defined(__cplusplus)
}
#endif

#endif
