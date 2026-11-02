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
 * Common hook framework (former shared module, MCS-private since the 2026-10 reorg).
 * Provides RUNTIME_HOOK macros for defining and calling intercepted runtime functions.
 */

#ifndef __COMMON_RUNTIME_HOOK_H__
#define __COMMON_RUNTIME_HOOK_H__

#include <dlfcn.h>
#include <stdint.h>
#include <stdio.h>

#include "ascend_compat.h"

/* rtError_t is provided by ascend_compat.h (or by <runtime/rt.h> in Ascend SDK) */

#if defined(__cplusplus)
extern "C" {
#endif

typedef struct {
    void *func_ptr;
    char *name;
} rt_entry_t;

typedef rtError_t (*rt_symbol_t)();

#define RUNTIME_HOOK_ENUM(x) HOOK_##x

#define RUNTIME_HOOK_DEFINE(func_name, ...) __attribute__((visibility("default"))) rtError_t func_name(__VA_ARGS__)

#define RUNTIME_FIND_ENTRY(table, symbol) ({ (table)[RUNTIME_HOOK_ENUM(symbol)].func_ptr; })

#define RUNTIME_HOOK_CALL(table, symbol, ...)                                \
    ({                                                                       \
        rt_symbol_t _entry = (rt_symbol_t)RUNTIME_FIND_ENTRY(table, symbol); \
        if (!_entry) {                                                       \
            fprintf(stderr, "HOOK ERROR: %s - %s\n", #symbol, dlerror());    \
        }                                                                    \
        _entry ? _entry(__VA_ARGS__) : -1;                                   \
    })

typedef enum
{
    /* Init Part */
    RUNTIME_HOOK_ENUM(rtSetDevice),
    RUNTIME_HOOK_ENUM(rtSetDeviceEx),
    RUNTIME_HOOK_ENUM(rtSetDeviceWithFlags),
    RUNTIME_HOOK_ENUM(rtSetDeviceWithoutTsd),
    RUNTIME_HOOK_ENUM(rtGetSocVersion),
    RUNTIME_HOOK_ENUM(rtGetDeviceInfo),
    /* Memory Part */
    RUNTIME_HOOK_ENUM(rtMalloc),
    RUNTIME_HOOK_ENUM(rtMallocCached),
    RUNTIME_HOOK_ENUM(rtDvppMalloc),
    RUNTIME_HOOK_ENUM(rtDvppMallocWithFlag),
    RUNTIME_HOOK_ENUM(rtMemAlloc),
    RUNTIME_HOOK_ENUM(rtMemAllocManaged),
    RUNTIME_HOOK_ENUM(rtMallocPhysical),
    RUNTIME_HOOK_ENUM(rtMemGetInfoEx),
    /* Kernel Part */
    RUNTIME_HOOK_ENUM(rtKernelLaunch),
    RUNTIME_HOOK_ENUM(rtKernelLaunchWithHandle),
    RUNTIME_HOOK_ENUM(rtKernelLaunchWithHandleV2),
    RUNTIME_HOOK_ENUM(rtKernelLaunchWithFlag),
    RUNTIME_HOOK_ENUM(rtKernelLaunchWithFlagV2),
    RUNTIME_HOOK_ENUM(rtKernelLaunchEx),
    RUNTIME_HOOK_ENUM(rtKernelLaunchFwk),
    RUNTIME_HOOK_ENUM(rtCpuKernelLaunch),
    RUNTIME_HOOK_ENUM(rtAicpuKernelLaunch),
    RUNTIME_HOOK_ENUM(rtCpuKernelLaunchWithFlag),
    RUNTIME_HOOK_ENUM(rtAicpuKernelLaunchWithFlag),
    RUNTIME_HOOK_ENUM(rtAicpuKernelLaunchExWithArgs),
    RUNTIME_HOOK_ENUM(rtLaunchKernelByFuncHandle),
    RUNTIME_HOOK_ENUM(rtLaunchKernelByFuncHandleV2),
    RUNTIME_HOOK_ENUM(rtLaunchKernelByFuncHandleV3),
    RUNTIME_HOOK_ENUM(rtVectorCoreKernelLaunchWithHandle),
    RUNTIME_HOOK_ENUM(rtVectorCoreKernelLaunch),
    RUNTIME_HOOK_ENUM(rtFftsPlusTaskLaunch),
    RUNTIME_HOOK_ENUM(rtFftsPlusTaskLaunchWithFlag),
    RUNTIME_HOOK_ENUM(rtFftsTaskLaunch),
    RUNTIME_HOOK_ENUM(rtFftsTaskLaunchWithFlag),
    RUNTIME_HOOK_ENUM(rtModelExecute),
    RUNTIME_HOOK_ENUM(rtModelExecuteAsync),
    RUNTIME_HOOK_ENUM(rtStreamBeginCapture),
    RUNTIME_HOOK_ENUM(rtStreamEndCapture),
    RUNTIME_HOOK_ENUM(rtsModelExecute),
    RUNTIME_HOOK_ENUM(rtModelExecuteSync),
    RUNTIME_HOOK_ENUM(rtStarsTaskLaunch),
    RUNTIME_HOOK_ENUM(rtStarsTaskLaunchWithFlag),
    RUNTIME_HOOK_ENUM(rtCmoTaskLaunch),
    RUNTIME_HOOK_ENUM(rtCmoAddrTaskLaunch),
    RUNTIME_HOOK_ENUM(rtBarrierTaskLaunch),
    RUNTIME_HOOK_ENUM(rtMultipleTaskInfoLaunch),
    RUNTIME_HOOK_ENUM(rtMultipleTaskInfoLaunchWithFlag),
    RUNTIME_HOOK_ENUM(rtsModelExecuteAsync),
    RUNTIME_HOOK_ENUM(rtsLaunchKernelWithHostArgs),
    RUNTIME_HOOK_ENUM(rtsLaunchCpuKernel),
    RUNTIME_HOOK_ENUM(rtsLaunchKernelWithConfig),
    RUNTIME_HOOK_ENUM(rtsLaunchKernelWithDevArgs),
    RUNTIME_HOOK_ENUM(rtsLaunchRandomNumTask),
    RUNTIME_HOOK_ENUM(rtsLaunchReduceAsyncTask),
    RUNTIME_HOOK_ENUM(rtsLaunchUpdateTask),
    /* Event Part */
    RUNTIME_HOOK_ENUM(rtEventCreate),
    RUNTIME_HOOK_ENUM(rtsEventCreate),
    RUNTIME_HOOK_ENUM(rtsEventCreateEx),
    RUNTIME_HOOK_ENUM(rtEventCreateWithFlag),
    RUNTIME_HOOK_ENUM(rtEventCreateExWithFlag),
    RUNTIME_HOOK_ENUM(rtStreamWaitEvent),
    RUNTIME_HOOK_ENUM(rtEventRecord),
    RUNTIME_HOOK_ENUM(rtEventDestroy),
    RUNTIME_HOOK_ENUM(rtEventReset),
    RUNTIME_HOOK_ENUM(rtsNotifyCreate),
    RUNTIME_HOOK_ENUM(rtNotifyRecord),
    RUNTIME_HOOK_ENUM(rtNotifyDestroy),
    RUNTIME_HOOK_ENUM(rtsNotifyWaitAndReset),
    RUNTIME_HOOK_ENUM(rtStreamWaitEventWithTimeout),
    RUNTIME_HOOK_ENUM(rtEventDestroySync),
    RUNTIME_HOOK_ENUM(rtNotifyCreate),
    RUNTIME_HOOK_ENUM(rtNotifyCreateWithFlag),
    RUNTIME_HOOK_ENUM(rtNotifyWait),
    RUNTIME_HOOK_ENUM(rtNotifyWaitWithTimeOut),
    RUNTIME_HOOK_ENUM(rtCntNotifyCreate),
    RUNTIME_HOOK_ENUM(rtCntNotifyCreateWithFlag),
    RUNTIME_HOOK_ENUM(rtCntNotifyRecord),
    RUNTIME_HOOK_ENUM(rtCntNotifyWaitWithTimeout),
    RUNTIME_HOOK_ENUM(rtCntNotifyDestroy),
    RUNTIME_HOOK_ENUM(rtsCntNotifyRecord),
    RUNTIME_HOOK_ENUM(rtsCntNotifyWaitWithTimeout),
    /* Other Part */
    RUNTIME_HOOK_ENUM(rtStreamSynchronize),
    RUNTIME_HOOK_ENUM(rtStreamDestroy),
    RUNTIME_HOOK_ENUM(rtDestroyStreamForce),
    RUNTIME_ENTRY_END,
} rt_hook_enum_t;

extern rt_entry_t rt_library_entry[];

extern void load_rt_libraries(void);

#if defined(__cplusplus)
}
#endif

#endif /* __COMMON_RUNTIME_HOOK_H__ */
