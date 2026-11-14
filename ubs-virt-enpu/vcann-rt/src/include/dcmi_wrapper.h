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

#ifndef __DCMI_WRAPPER_H__
#define __DCMI_WRAPPER_H__

#include <dcmi_interface_api.h>
#include "common.h"

#if defined(__cplusplus)
extern "C" {
#endif

#define MAX_PIDS 64

typedef enum
{
    ENPU_DCMI_UTIL_AICORE = 2,
    ENPU_DCMI_UTIL_AICPU = 3,
    ENPU_DCMI_UTIL_CTRLCPU = 4,
    ENPU_DCMI_UTIL_VECTOR = 12,
    ENPU_DCMI_UTIL_NPU = 13,
    ENPU_DCMI_UTIL_CUBE = 14,
} enpu_dcmi_utilization_type;

typedef enum
{
    SOC_VERSION_ASCEND_950,
    SOC_VERSION_ASCEND_910,
    SOC_VERSION_ASCEND_310
} soc_version;

// 定义回调函数类型
typedef int (*dcmi_init_func)(void);

typedef int (*dcmi_get_device_utilization_rate_func)(int logic_id, int card_id, int device_id, int input_type,
                                                     unsigned int *utilization_rate);

typedef int (*dcmi_get_device_resource_info_func)(int logic_id, int card_id, int device_id,
                                                  struct dcmi_proc_mem_info *proc_info, int *proc_num);

typedef int (*dcmi_aicore_num_func)(int logic_id, int card_id, int device_id, unsigned int *aicore_num);

typedef struct {
    dcmi_init_func init_callback;
    dcmi_get_device_utilization_rate_func get_device_utilization_rate_callback;
    dcmi_get_device_resource_info_func get_device_resource_info_callback;
    dcmi_aicore_num_func get_device_aicore_num_callback;
} dcmi_operations;

extern int enpu_dcmi_get_card_info(uint32_t phy_id, int *card_id, int *device_id, int *logic_id, uint8_t soc_version);
/* Legacy limiter/swap entry: temporary worker thread, generic success/failure.
 * Query, summation and validation delegate to enpu_dcmi_query_hbm_usage(). */
extern int enpu_dcmi_get_device_resource_info(int logic_id, int card_id, int device_id, size_t *used);
/* Shared direct query for monitor, sampler and the legacy threaded wrapper.
 * No nested thread and no PID filtering.
 * Returns raw DCMI errors or negative errno for local validation failures.
 */
extern int enpu_dcmi_query_hbm_usage(int logic_id, int card_id, int device_id, uint8_t soc_version,
                                     uint64_t *used_bytes);
extern int enpu_dcmi_get_device_utilization_rate(int logic_id, int card_id, int device_id,
                                                 unsigned int *utilization_rate);
extern int enpu_dcmi_get_aicore_utilization_rate(int logic_id, int card_id, int device_id,
                                                 unsigned int *utilization_rate);
extern int enpu_dcmi_get_aicore_num(int logic_id, int card_id, int device_id, unsigned int *aicore_num);
/* Raw per-type query: no cross-metric fallback; preserves the original DCMI return code. */
extern int enpu_dcmi_get_utilization_rate(int logic_id, int card_id, int device_id, uint8_t soc_version, int input_type,
                                          unsigned int *utilization_rate);
extern int register_callback(uint8_t soc_version);

#if defined(__cplusplus)
}
#endif

#endif
