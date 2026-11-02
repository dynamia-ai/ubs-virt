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
 * MCS runtime API resolution layer - typedefs, real function pointers and
 * dlsym-based symbol loading for the Ascend runtime functions that MCS
 * intercepts.
 */

#ifndef MCS_RTAPI_H
#define MCS_RTAPI_H

#include <stddef.h>
#include <stdint.h>

#if defined(__cplusplus)
extern "C" {
#endif

/* Same guard as common/ascend_compat.h to avoid redefinition */
#ifndef __RT_ERROR_T_DEFINED__
#define __RT_ERROR_T_DEFINED__
typedef int32_t rtError_t;
#endif /* __RT_ERROR_T_DEFINED__ */

typedef rtError_t (*rtSetDevice_fn)(int32_t devId);
typedef rtError_t (*rtGetDeviceInfo_fn)(uint32_t deviceId, int32_t moduleType, int32_t infoType, int64_t *val);
typedef rtError_t (*rtGetAiCoreCount_fn)(uint32_t *aiCoreCnt);
typedef rtError_t (*rtKernelLaunch_fn)(const void *stubFunc, uint32_t blockDim, void *args, uint32_t argsSize,
                                       void *smDesc, void *stm);
typedef rtError_t (*rtKernelLaunchWithHandle_fn)(void *hdl, const uint64_t tilingKey, uint32_t blockDim, void *argsInfo,
                                                 void *smDesc, void *stm, const void *kernelInfo);
typedef rtError_t (*rtKernelLaunchWithHandleV2_fn)(void *hdl, const uint64_t tilingKey, uint32_t blockDim,
                                                   void *argsInfo, void *smDesc, void *stm, const void *cfgInfo);
typedef rtError_t (*rtKernelLaunchWithFlag_fn)(const void *stubFunc, uint32_t blockDim, void *argsInfo, void *smDesc,
                                               void *stm, uint32_t flags);
typedef rtError_t (*rtKernelLaunchWithFlagV2_fn)(const void *stubFunc, uint32_t blockDim, void *argsInfo, void *smDesc,
                                                 void *stm, uint32_t flags, const void *cfgInfo);
typedef rtError_t (*rtVectorCoreKernelLaunchWithHandle_fn)(void *hdl, const uint64_t tilingKey, uint32_t blockDim,
                                                           void *argsInfo, void *smDesc, void *stm,
                                                           const void *cfgInfo);
typedef rtError_t (*rtVectorCoreKernelLaunch_fn)(const void *stubFunc, uint32_t blockDim, void *argsInfo, void *smDesc,
                                                 void *stm, uint32_t flags, const void *cfgInfo);

/* Newer "framework" launch entry points (no direct blockDim in signature) */
typedef rtError_t (*rtKernelLaunchEx_fn)(void *args, uint32_t argsSize, uint32_t flags, void *stm);
typedef rtError_t (*rtKernelLaunchFwk_fn)(const char *opName, void *args, uint32_t argsSize, uint32_t flags, void *stm);

/* funcHandle-based launch (ACL C API path) with explicit numBlocks/blockDim */
typedef rtError_t (*rtLaunchKernelByFuncHandle_fn)(void *funcHandle, uint32_t numBlocks, void *argsHandle, void *stm);
typedef rtError_t (*rtLaunchKernelByFuncHandleV2_fn)(void *funcHandle, uint32_t numBlocks, void *argsHandle, void *stm,
                                                     const void *cfgInfo);
typedef rtError_t (*rtLaunchKernelByFuncHandleV3_fn)(void *funcHandle, uint32_t numBlocks, const void *argsInfo,
                                                     void *stm, const void *cfgInfo);

/* rts kernel launch (ACL C API internal path) with explicit numBlocks/blockDim */
typedef rtError_t (*rtsLaunchKernelWithDevArgs_fn)(void *funcHandle, uint32_t numBlocks, void *stm, void *cfg,
                                                   const void *args, uint32_t argsSize, void *reserve);
typedef rtError_t (*rtsLaunchKernelWithConfig_fn)(void *funcHandle, uint32_t numBlocks, void *stm, void *cfg,
                                                  void *argsHandle, void *reserve);
typedef rtError_t (*rtsLaunchKernelWithHostArgs_fn)(void *funcHandle, uint32_t numBlocks, void *stm, void *cfg,
                                                    void *hostArgs, uint32_t argsSize, void *placeHolderArray,
                                                    uint32_t placeHolderNum);
typedef rtError_t (*rtLaunchKernelWithArgsArray_fn)(void *func, uint32_t numBlocks, void *stm, void *cfg, void **args);

/* rtsGetResInCurrentThread: query the per-thread Cube/Vector Core resource
 * limit (rtDevResLimitType_t: RT_DEV_RES_CUBE_CORE=0, RT_DEV_RES_VECTOR_CORE=1) */
typedef rtError_t (*rtsGetResInCurrentThread_fn)(int32_t type, uint32_t *value);

/* Runtime APIs for kernel type query */

/* Function registration (used to build stubFunc → type mapping) */

/* Real function pointers, resolved by load_real_functions() */
extern rtSetDevice_fn real_rtSetDevice;
extern rtGetDeviceInfo_fn real_rtGetDeviceInfo;
extern rtGetAiCoreCount_fn real_rtGetAiCoreCount;
extern rtKernelLaunch_fn real_rtKernelLaunch;
extern rtKernelLaunchWithHandle_fn real_rtKernelLaunchWithHandle;
extern rtKernelLaunchWithHandleV2_fn real_rtKernelLaunchWithHandleV2;
extern rtKernelLaunchWithFlag_fn real_rtKernelLaunchWithFlag;
extern rtKernelLaunchWithFlagV2_fn real_rtKernelLaunchWithFlagV2;
extern rtVectorCoreKernelLaunchWithHandle_fn real_rtVectorCoreKernelLaunchWithHandle;
extern rtVectorCoreKernelLaunch_fn real_rtVectorCoreKernelLaunch;
extern rtKernelLaunchEx_fn real_rtKernelLaunchEx;
extern rtKernelLaunchFwk_fn real_rtKernelLaunchFwk;
extern rtLaunchKernelByFuncHandle_fn real_rtLaunchKernelByFuncHandle;
extern rtLaunchKernelByFuncHandleV2_fn real_rtLaunchKernelByFuncHandleV2;
extern rtLaunchKernelByFuncHandleV3_fn real_rtLaunchKernelByFuncHandleV3;
extern rtsLaunchKernelWithDevArgs_fn real_rtsLaunchKernelWithDevArgs;
extern rtsLaunchKernelWithConfig_fn real_rtsLaunchKernelWithConfig;
extern rtsLaunchKernelWithHostArgs_fn real_rtsLaunchKernelWithHostArgs;
extern rtLaunchKernelWithArgsArray_fn real_rtLaunchKernelWithArgsArray;
extern rtsGetResInCurrentThread_fn real_rtsGetResInCurrentThread;

/* Populate the common hook table (rt_library_entry[]) with real function pointers */
void load_rt_libraries(void);

/* Resolve all real_* function pointers via dlsym */
void load_real_functions(void);

#if defined(__cplusplus)
}
#endif

#endif /* MCS_RTAPI_H */
