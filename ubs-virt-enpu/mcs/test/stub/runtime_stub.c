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

#include "runtime_stub.h"

#include <dlfcn.h>
#include <string.h>

#include "ascend_compat.h"
#include "mcs_device.h"
#include "mcs_rtapi.h"

int g_stub_symbols_available = 1;
int g_stub_set_device_return = 0;
int g_stub_core_count_return = 0;
int g_stub_device_info_return = 0;
int g_stub_launch_return = 77;
int g_stub_res_return = 88;
int g_stub_malloc_return = 0;
int g_stub_free_return = 0;
int g_stub_set_device_calls = 0;
uint32_t g_stub_physical_aicube = 32;
uint32_t g_stub_physical_aivector = 16;
uint32_t g_stub_res_value = 99;

void reset_runtime_stub(void)
{
    g_stub_symbols_available = 1;
    g_stub_set_device_return = 0;
    g_stub_core_count_return = 0;
    g_stub_device_info_return = 0;
    g_stub_launch_return = 77;
    g_stub_res_return = 88;
    g_stub_malloc_return = 0;
    g_stub_free_return = 0;
    g_stub_set_device_calls = 0;
    g_stub_physical_aicube = 32;
    g_stub_physical_aivector = 16;
    g_stub_res_value = 99;
}

void *__wrap_dlopen(const char *filename, int flags)
{
    (void)filename;
    (void)flags;
    return g_stub_symbols_available ? (void *)(uintptr_t)1 : NULL;
}

static rtError_t stub_generic(void)
{
    return 0;
}

#ifdef MCS_TEST_MAIN_SUITE
static rtError_t stub_rtSetDevice(int32_t devId)
{
    (void)devId;
    g_stub_set_device_calls++;
    return g_stub_set_device_return;
}

static rtError_t stub_rtGetAiCoreCount(uint32_t *aiCoreCnt)
{
    if (aiCoreCnt != NULL) {
        *aiCoreCnt = g_stub_physical_aicube;
    }
    return g_stub_core_count_return;
}

static rtError_t stub_rtGetDeviceInfo(uint32_t deviceId, int32_t moduleType, int32_t infoType, int64_t *val)
{
    (void)deviceId;
    (void)infoType;
    if (val != NULL) {
        *val = moduleType == MCS_MODULE_TYPE_VECTOR_CORE ? g_stub_physical_aivector : g_stub_physical_aicube;
    }
    return g_stub_device_info_return;
}

#define STUB_LAUNCH(name)            \
    static rtError_t name(void)      \
    {                                \
        return g_stub_launch_return; \
    }

STUB_LAUNCH(stub_rtKernelLaunch)
STUB_LAUNCH(stub_rtKernelLaunchWithHandle)
STUB_LAUNCH(stub_rtKernelLaunchWithHandleV2)
STUB_LAUNCH(stub_rtKernelLaunchWithFlag)
STUB_LAUNCH(stub_rtKernelLaunchWithFlagV2)
STUB_LAUNCH(stub_rtVectorCoreKernelLaunchWithHandle)
STUB_LAUNCH(stub_rtVectorCoreKernelLaunch)
STUB_LAUNCH(stub_rtKernelLaunchEx)
STUB_LAUNCH(stub_rtKernelLaunchFwk)
STUB_LAUNCH(stub_rtLaunchKernelByFuncHandle)
STUB_LAUNCH(stub_rtLaunchKernelByFuncHandleV2)
STUB_LAUNCH(stub_rtLaunchKernelByFuncHandleV3)
STUB_LAUNCH(stub_rtsLaunchKernelWithDevArgs)
STUB_LAUNCH(stub_rtsLaunchKernelWithConfig)
STUB_LAUNCH(stub_rtsLaunchKernelWithHostArgs)
STUB_LAUNCH(stub_rtLaunchKernelWithArgsArray)

static rtError_t stub_rtsGetResInCurrentThread(int32_t type, uint32_t *value)
{
    (void)type;
    if (value != NULL) {
        *value = g_stub_res_value;
    }
    return g_stub_res_return;
}

void activate_mem_hooks(void) {}

#else
static uintptr_t g_next_dev_ptr = 0x10000;
static rtDrvMemHandle g_next_handle = 1;

static rtError_t stub_alloc(void **devPtr)
{
    if (devPtr != NULL) {
        g_next_dev_ptr += 0x1000;
        *devPtr = (void *)g_next_dev_ptr;
    }
    return g_stub_malloc_return;
}

static rtError_t stub_rtMallocPhysical(rtDrvMemHandle *handle)
{
    if (handle != NULL) {
        *handle = g_next_handle++;
    }
    return g_stub_malloc_return;
}

static rtError_t stub_free(void *devPtr)
{
    (void)devPtr;
    return g_stub_free_return;
}

static rtError_t stub_rtMemGetInfoEx(rtMemInfoType_t memInfoType, size_t *freeSize, size_t *totalSize)
{
    (void)memInfoType;
    if (freeSize != NULL) {
        *freeSize = 4096;
    }
    if (totalSize != NULL) {
        *totalSize = 8192;
    }
    return 23;
}
#endif

void *__wrap_dlsym(void *handle, const char *symbol)
{
    (void)handle;
    if (!g_stub_symbols_available || symbol == NULL) {
        return NULL;
    }

#ifdef MCS_TEST_MAIN_SUITE
#define MAP_SYMBOL(name, stub)            \
    if (strcmp(symbol, name) == 0) {      \
        return (void *)(uintptr_t)(stub); \
    }
    MAP_SYMBOL("rtSetDevice", stub_rtSetDevice)
    MAP_SYMBOL("rtGetDeviceInfo", stub_rtGetDeviceInfo)
    MAP_SYMBOL("rtGetAiCoreCount", stub_rtGetAiCoreCount)
    MAP_SYMBOL("rtKernelLaunch", stub_rtKernelLaunch)
    MAP_SYMBOL("rtKernelLaunchWithHandle", stub_rtKernelLaunchWithHandle)
    MAP_SYMBOL("rtKernelLaunchWithHandleV2", stub_rtKernelLaunchWithHandleV2)
    MAP_SYMBOL("rtKernelLaunchWithFlag", stub_rtKernelLaunchWithFlag)
    MAP_SYMBOL("rtKernelLaunchWithFlagV2", stub_rtKernelLaunchWithFlagV2)
    MAP_SYMBOL("rtVectorCoreKernelLaunchWithHandle", stub_rtVectorCoreKernelLaunchWithHandle)
    MAP_SYMBOL("rtVectorCoreKernelLaunch", stub_rtVectorCoreKernelLaunch)
    MAP_SYMBOL("rtKernelLaunchEx", stub_rtKernelLaunchEx)
    MAP_SYMBOL("rtKernelLaunchFwk", stub_rtKernelLaunchFwk)
    MAP_SYMBOL("rtLaunchKernelByFuncHandle", stub_rtLaunchKernelByFuncHandle)
    MAP_SYMBOL("rtLaunchKernelByFuncHandleV2", stub_rtLaunchKernelByFuncHandleV2)
    MAP_SYMBOL("rtLaunchKernelByFuncHandleV3", stub_rtLaunchKernelByFuncHandleV3)
    MAP_SYMBOL("rtsLaunchKernelWithDevArgs", stub_rtsLaunchKernelWithDevArgs)
    MAP_SYMBOL("rtsLaunchKernelWithConfig", stub_rtsLaunchKernelWithConfig)
    MAP_SYMBOL("rtsLaunchKernelWithHostArgs", stub_rtsLaunchKernelWithHostArgs)
    MAP_SYMBOL("rtLaunchKernelWithArgsArray", stub_rtLaunchKernelWithArgsArray)
    MAP_SYMBOL("rtsGetResInCurrentThread", stub_rtsGetResInCurrentThread)
#undef MAP_SYMBOL
#else
    if (strcmp(symbol, "rtMallocPhysical") == 0) {
        return (void *)(uintptr_t)stub_rtMallocPhysical;
    }
    if (strcmp(symbol, "rtFree") == 0 || strcmp(symbol, "rtDvppFree") == 0 || strcmp(symbol, "rtMemFree") == 0) {
        return (void *)(uintptr_t)stub_free;
    }
    if (strcmp(symbol, "rtMemGetInfoEx") == 0) {
        return (void *)(uintptr_t)stub_rtMemGetInfoEx;
    }
    if (strcmp(symbol, "rtMalloc") == 0 || strcmp(symbol, "rtMallocCached") == 0 ||
        strcmp(symbol, "rtDvppMalloc") == 0 || strcmp(symbol, "rtDvppMallocWithFlag") == 0 ||
        strcmp(symbol, "rtMemAlloc") == 0 || strcmp(symbol, "rtMemAllocManaged") == 0) {
        return (void *)(uintptr_t)stub_alloc;
    }
#endif
    return (void *)(uintptr_t)stub_generic;
}
