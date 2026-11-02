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
 * Common hook table (former shared module, MCS-private since the 2026-10 reorg).
 * Defines rt_library_entry[] table and load_rt_libraries() for resolving
 * real runtime function pointers via dlsym.
 */

#include "runtime_hook.h"

rt_entry_t rt_library_entry[] = {
    /* Init Part */
    {.name = "rtSetDevice"},
    {.name = "rtSetDeviceEx"},
    {.name = "rtSetDeviceWithFlags"},
    {.name = "rtSetDeviceWithoutTsd"},
    {.name = "rtGetSocVersion"},
    {.name = "rtGetDeviceInfo"},
    /* Memory Part */
    {.name = "rtMalloc"},
    {.name = "rtMallocCached"},
    {.name = "rtDvppMalloc"},
    {.name = "rtDvppMallocWithFlag"},
    {.name = "rtMemAlloc"},
    {.name = "rtMemAllocManaged"},
    {.name = "rtMallocPhysical"},
    {.name = "rtMemGetInfoEx"},
    /* Kernel Part */
    {.name = "rtKernelLaunch"},
    {.name = "rtKernelLaunchWithHandle"},
    {.name = "rtKernelLaunchWithHandleV2"},
    {.name = "rtKernelLaunchWithFlag"},
    {.name = "rtKernelLaunchWithFlagV2"},
    {.name = "rtKernelLaunchEx"},
    {.name = "rtKernelLaunchFwk"},
    {.name = "rtCpuKernelLaunch"},
    {.name = "rtAicpuKernelLaunch"},
    {.name = "rtCpuKernelLaunchWithFlag"},
    {.name = "rtAicpuKernelLaunchWithFlag"},
    {.name = "rtAicpuKernelLaunchExWithArgs"},
    {.name = "rtLaunchKernelByFuncHandle"},
    {.name = "rtLaunchKernelByFuncHandleV2"},
    {.name = "rtLaunchKernelByFuncHandleV3"},
    {.name = "rtVectorCoreKernelLaunchWithHandle"},
    {.name = "rtVectorCoreKernelLaunch"},
    {.name = "rtFftsPlusTaskLaunch"},
    {.name = "rtFftsPlusTaskLaunchWithFlag"},
    {.name = "rtFftsTaskLaunch"},
    {.name = "rtFftsTaskLaunchWithFlag"},
    {.name = "rtModelExecute"},
    {.name = "rtModelExecuteAsync"},
    {.name = "rtStreamBeginCapture"},
    {.name = "rtStreamEndCapture"},
    {.name = "rtsModelExecute"},
    {.name = "rtModelExecuteSync"},
    {.name = "rtStarsTaskLaunch"},
    {.name = "rtStarsTaskLaunchWithFlag"},
    {.name = "rtCmoTaskLaunch"},
    {.name = "rtCmoAddrTaskLaunch"},
    {.name = "rtBarrierTaskLaunch"},
    {.name = "rtMultipleTaskInfoLaunch"},
    {.name = "rtMultipleTaskInfoLaunchWithFlag"},
    {.name = "rtsModelExecuteAsync"},
    {.name = "rtsLaunchKernelWithHostArgs"},
    {.name = "rtsLaunchCpuKernel"},
    {.name = "rtsLaunchKernelWithConfig"},
    {.name = "rtsLaunchKernelWithDevArgs"},
    {.name = "rtsLaunchRandomNumTask"},
    {.name = "rtsLaunchReduceAsyncTask"},
    {.name = "rtsLaunchUpdateTask"},
    /* Event Part */
    {.name = "rtEventCreate"},
    {.name = "rtsEventCreate"},
    {.name = "rtsEventCreateEx"},
    {.name = "rtEventCreateWithFlag"},
    {.name = "rtEventCreateExWithFlag"},
    {.name = "rtStreamWaitEvent"},
    {.name = "rtEventRecord"},
    {.name = "rtEventDestroy"},
    {.name = "rtEventReset"},
    {.name = "rtsNotifyCreate"},
    {.name = "rtNotifyRecord"},
    {.name = "rtNotifyDestroy"},
    {.name = "rtsNotifyWaitAndReset"},
    {.name = "rtStreamWaitEventWithTimeout"},
    {.name = "rtEventDestroySync"},
    {.name = "rtNotifyCreate"},
    {.name = "rtNotifyCreateWithFlag"},
    {.name = "rtNotifyWait"},
    {.name = "rtNotifyWaitWithTimeOut"},
    {.name = "rtCntNotifyCreate"},
    {.name = "rtCntNotifyCreateWithFlag"},
    {.name = "rtCntNotifyRecord"},
    {.name = "rtCntNotifyWaitWithTimeout"},
    {.name = "rtCntNotifyDestroy"},
    {.name = "rtsCntNotifyRecord"},
    {.name = "rtsCntNotifyWaitWithTimeout"},
    /* Other Part */
    {.name = "rtStreamSynchronize"},
    {.name = "rtStreamDestroy"},
    {.name = "rtDestroyStreamForce"},
};

/* load_rt_libraries() is provided by each project:
 * - vcann-rt: in device.c (via pthread_once)
 * - MCS: in mcs_intercept.c / mcs_mem.c
 */
