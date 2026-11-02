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

#include "mcs_device.h"
#include <stddef.h>
#include "mcs_config.h"

static int32_t g_physical_aicube = 0;
static int32_t g_physical_aivector = 0;
static int32_t g_limited_aicube = 0;
static int32_t g_limited_aivector = 0;
static int g_mcs_device_initialized = 0;

void mcs_device_init(int32_t physical_aicube, int32_t physical_aivector, int32_t quota_percent)
{
    g_physical_aicube = physical_aicube;
    g_physical_aivector = physical_aivector;
    g_limited_aicube = mcs_calc_limited_aicore(physical_aicube, quota_percent);
    g_limited_aivector = mcs_calc_limited_aicore(physical_aivector, quota_percent);
    g_mcs_device_initialized = 1;
}

int32_t mcs_get_limited_aicube(void)
{
    return g_limited_aicube;
}

int32_t mcs_get_limited_aivector(void)
{
    return g_limited_aivector;
}

mcs_rtError_t mcs_rtGetDeviceInfo_hook(uint32_t deviceId, int32_t moduleType, int32_t infoType, int64_t *val)
{
    (void)deviceId;

    if (val == NULL) {
        return -1;
    }

    if (!g_mcs_device_initialized) {
        return -1;
    }

    if (moduleType == MCS_MODULE_TYPE_AICORE &&
        (infoType == MCS_INFO_TYPE_CORE_NUM || infoType == (int32_t)MCS_INFO_TYPE_CUBE_NUM)) {
        *val = (int64_t)g_limited_aicube;
        return MCS_RT_ERROR_NONE;
    }

    if (moduleType == MCS_MODULE_TYPE_VECTOR_CORE && infoType == MCS_INFO_TYPE_CORE_NUM) {
        *val = (int64_t)g_limited_aivector;
        return MCS_RT_ERROR_NONE;
    }

    /* Not a query we handle - return error to indicate pass-through needed */
    return -1;
}
