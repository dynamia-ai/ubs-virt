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
 *
 * Ascend type compatibility definitions for the common module.
 * Provides minimal type definitions needed when building without the
 * full Ascend SDK. When the SDK is available, these should match.
 */

#ifndef __ASCEND_COMPAT_H__
#define __ASCEND_COMPAT_H__

#include <stdint.h>

#if defined(__cplusplus)
extern "C" {
#endif

/* ACL error codes (from acl/acl.h) */
#define ACL_ERROR_FAILURE 500000
#define ACL_ERROR_STORAGE_OVER_LIMIT 500006

/* Runtime types (from runtime/rt.h / rt_external_base.h) */
#ifndef __RT_ERROR_T_DEFINED__
#define __RT_ERROR_T_DEFINED__
typedef int32_t rtError_t;
#endif /* __RT_ERROR_T_DEFINED__ */

typedef int32_t rtMemType_t;
typedef int32_t rtMallocPolicy;
typedef int32_t rtMallocAdvise;
typedef int32_t rtMallocConfig_t;
typedef int32_t rtDrvMemHandle;
typedef int32_t rtDrvMemProp_t;
typedef int32_t rtMemInfoType_t;

#if defined(__cplusplus)
}
#endif

#endif /* __ASCEND_COMPAT_H__ */
