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

#ifndef MCS_DEVICE_H
#define MCS_DEVICE_H

#include <stdint.h>

#if defined(__cplusplus)
extern "C" {
#endif

/* Module types (from rtDeviceModuleType_t) */
#define MCS_MODULE_TYPE_AICORE 4
#define MCS_MODULE_TYPE_VECTOR_CORE 7

/* Info types (from DEV_INFO_TYPE) */
#define MCS_INFO_TYPE_CORE_NUM 3
#define MCS_INFO_TYPE_CUBE_NUM 0x775A5A5AU

/* Device resource limit types (from rtDevResLimitType_t in runtime base.h) */
#define RT_DEV_RES_CUBE_CORE 0
#define RT_DEV_RES_VECTOR_CORE 1
#define RT_DEV_RES_TYPE_MAX 2

#define MCS_RT_ERROR_NONE 0

typedef int32_t mcs_rtError_t;

/* Initialize MCS device info with physical counts and quota */
void mcs_device_init(int32_t physical_aicube, int32_t physical_aivector, int32_t quota_percent);

/* Get the limited aicube count */
int32_t mcs_get_limited_aicube(void);

/* Get the limited aivector count */
int32_t mcs_get_limited_aivector(void);

/* Hook function for rtGetDeviceInfo - returns limited counts for aicore queries */
mcs_rtError_t mcs_rtGetDeviceInfo_hook(uint32_t deviceId, int32_t moduleType, int32_t infoType, int64_t *val);

#if defined(__cplusplus)
}
#endif

#endif /* MCS_DEVICE_H */
