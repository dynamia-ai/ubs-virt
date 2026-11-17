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

#ifndef MCS_INTERCEPT_API_H
#define MCS_INTERCEPT_API_H

#include "mcs_rtapi.h"

#if defined(__cplusplus)
extern "C" {
#endif

rtError_t rtSetDevice(int32_t devId);
rtError_t rtGetDeviceInfo(uint32_t deviceId, int32_t moduleType, int32_t infoType, int64_t *val);
rtError_t rtGetAiCoreCount(uint32_t *aiCoreCnt);
rtError_t rtKernelLaunch(const void *stubFunc, uint32_t blockDim, void *args, uint32_t argsSize, void *smDesc,
                         void *stm);
rtError_t rtKernelLaunchWithHandle(void *hdl, const uint64_t tilingKey, uint32_t blockDim, void *argsInfo, void *smDesc,
                                   void *stm, const void *kernelInfo);
rtError_t rtKernelLaunchWithHandleV2(void *hdl, const uint64_t tilingKey, uint32_t blockDim, void *argsInfo,
                                     void *smDesc, void *stm, const void *cfgInfo);
rtError_t rtKernelLaunchWithFlag(const void *stubFunc, uint32_t blockDim, void *argsInfo, void *smDesc, void *stm,
                                 uint32_t flags);
rtError_t rtKernelLaunchWithFlagV2(const void *stubFunc, uint32_t blockDim, void *argsInfo, void *smDesc, void *stm,
                                   uint32_t flags, const void *cfgInfo);
rtError_t rtVectorCoreKernelLaunchWithHandle(void *hdl, const uint64_t tilingKey, uint32_t blockDim, void *argsInfo,
                                             void *smDesc, void *stm, const void *cfgInfo);
rtError_t rtVectorCoreKernelLaunch(const void *stubFunc, uint32_t blockDim, void *argsInfo, void *smDesc, void *stm,
                                   uint32_t flags, const void *cfgInfo);
rtError_t rtKernelLaunchEx(void *args, uint32_t argsSize, uint32_t flags, void *stm);
rtError_t rtKernelLaunchFwk(const char *opName, void *args, uint32_t argsSize, uint32_t flags, void *stm);
rtError_t rtLaunchKernelByFuncHandle(void *funcHandle, uint32_t numBlocks, void *argsHandle, void *stm);
rtError_t rtLaunchKernelByFuncHandleV2(void *funcHandle, uint32_t numBlocks, void *argsHandle, void *stm,
                                       const void *cfgInfo);
rtError_t rtLaunchKernelByFuncHandleV3(void *funcHandle, uint32_t numBlocks, const void *argsInfo, void *stm,
                                       const void *cfgInfo);
rtError_t rtsLaunchKernelWithDevArgs(void *funcHandle, uint32_t numBlocks, void *stm, void *cfg, const void *args,
                                     uint32_t argsSize, void *reserve);
rtError_t rtsLaunchKernelWithConfig(void *funcHandle, uint32_t numBlocks, void *stm, void *cfg, void *argsHandle,
                                    void *reserve);
rtError_t rtsLaunchKernelWithHostArgs(void *funcHandle, uint32_t numBlocks, void *stm, void *cfg, void *hostArgs,
                                      uint32_t argsSize, void *placeHolderArray, uint32_t placeHolderNum);
rtError_t rtLaunchKernelWithArgsArray(void *func, uint32_t numBlocks, void *stm, void *cfg, void **args);
rtError_t rtsGetResInCurrentThread(int32_t type, uint32_t *value);

#if defined(__cplusplus)
}
#endif

#endif
