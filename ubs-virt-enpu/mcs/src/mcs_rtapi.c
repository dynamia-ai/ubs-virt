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

#include "mcs_rtapi.h"

#include <dlfcn.h>

#include "mcs_log.h"
#include "runtime_hook.h"

/* Real function pointers */
rtSetDevice_fn real_rtSetDevice = NULL;
rtGetDeviceInfo_fn real_rtGetDeviceInfo = NULL;
rtGetAiCoreCount_fn real_rtGetAiCoreCount = NULL;
rtKernelLaunch_fn real_rtKernelLaunch = NULL;
rtKernelLaunchWithHandle_fn real_rtKernelLaunchWithHandle = NULL;
rtKernelLaunchWithHandleV2_fn real_rtKernelLaunchWithHandleV2 = NULL;
rtKernelLaunchWithFlag_fn real_rtKernelLaunchWithFlag = NULL;
rtKernelLaunchWithFlagV2_fn real_rtKernelLaunchWithFlagV2 = NULL;
rtVectorCoreKernelLaunchWithHandle_fn real_rtVectorCoreKernelLaunchWithHandle = NULL;
rtVectorCoreKernelLaunch_fn real_rtVectorCoreKernelLaunch = NULL;
rtKernelLaunchEx_fn real_rtKernelLaunchEx = NULL;
rtKernelLaunchFwk_fn real_rtKernelLaunchFwk = NULL;
rtLaunchKernelByFuncHandle_fn real_rtLaunchKernelByFuncHandle = NULL;
rtLaunchKernelByFuncHandleV2_fn real_rtLaunchKernelByFuncHandleV2 = NULL;
rtLaunchKernelByFuncHandleV3_fn real_rtLaunchKernelByFuncHandleV3 = NULL;
rtsLaunchKernelWithDevArgs_fn real_rtsLaunchKernelWithDevArgs = NULL;
rtsLaunchKernelWithConfig_fn real_rtsLaunchKernelWithConfig = NULL;
rtsLaunchKernelWithHostArgs_fn real_rtsLaunchKernelWithHostArgs = NULL;
rtLaunchKernelWithArgsArray_fn real_rtLaunchKernelWithArgsArray = NULL;
rtsGetResInCurrentThread_fn real_rtsGetResInCurrentThread = NULL;

/* Runtime library handle used as a fallback symbol source. Obtained lazily:
 * in a normal LD_PRELOAD deployment the runtime is already loaded (the app
 * links it), so dlopen simply returns the existing handle. RTLD_NEXT alone
 * is not reliable here: some runtime symbols are provided by dependency
 * libraries that are not always reachable from libmcs's link-map node. */
static void *g_runtime_handle = NULL;

static void *mcs_open_runtime(void)
{
    if (g_runtime_handle == NULL) {
        static const char *runtime_paths[] = {
            "libruntime.so",
            "/usr/local/Ascend/ascend-toolkit/latest/lib64/libruntime.so",
        };
        for (size_t i = 0; i < sizeof(runtime_paths) / sizeof(runtime_paths[0]); i++) {
            g_runtime_handle = dlopen(runtime_paths[i], RTLD_LAZY | RTLD_GLOBAL);
            if (g_runtime_handle != NULL) {
                LOG_INFO("loaded runtime from %s", runtime_paths[i]);
                break;
            }
        }
    }
    return g_runtime_handle;
}

/* When the runtime library cannot be loaded at all (e.g. very early startup),
 * symbol lookups cannot succeed yet. Retry that failing state at most once
 * per window: an unthrottled retry adds hundreds of microseconds to every
 * intercepted call (each missing symbol walks the ld.so exception path). */
static int mcs_retry_window_open(void)
{
    static struct timespec last;
    static int have_last = 0;
    struct timespec now;
    clock_gettime(CLOCK_MONOTONIC, &now);
    if (!have_last || now.tv_sec - last.tv_sec >= 2) {
        last = now;
        have_last = 1;
        return 1;
    }
    return 0;
}

void load_rt_libraries(void)
{
    /* Resolve each entry at most once per process (or once per retry window
     * while the runtime library is not loadable yet, e.g. early startup).
     * Re-scanning the table on every intercepted API call is prohibitively
     * expensive: many of these symbols do not exist in current runtime
     * releases, and each failed dlsym walks the ld.so exception path
     * (~10us), adding hundreds of microseconds of pure overhead to every
     * runtime call that passes through the intercepts (measured as a ~2x
     * end-to-end slowdown). */
    static uint8_t attempted[RUNTIME_ENTRY_END];

    int runtime_available = (mcs_open_runtime() != NULL);
    if (!runtime_available && !mcs_retry_window_open()) {
        return;
    }

    extern rt_entry_t rt_library_entry[];
    for (int32_t i = 0; i < RUNTIME_ENTRY_END; i++) {
        if (rt_library_entry[i].func_ptr == NULL) {
            if (!runtime_available && attempted[i]) {
                continue;
            }
            attempted[i] = 1;
            rt_library_entry[i].func_ptr = dlsym(RTLD_DEFAULT, rt_library_entry[i].name);
            if (rt_library_entry[i].func_ptr == NULL) {
                rt_library_entry[i].func_ptr = dlsym(RTLD_NEXT, rt_library_entry[i].name);
            }
            if (rt_library_entry[i].func_ptr == NULL && runtime_available) {
                rt_library_entry[i].func_ptr = dlsym(g_runtime_handle, rt_library_entry[i].name);
            }
        }
    }
}

void load_real_functions(void)
{
    /* Each real_* pointer is resolved at most once per process (or retried
     * while the runtime library is not loadable yet, e.g. early startup).
     * Re-running dlsym(RTLD_NEXT) on every intercepted API call is
     * prohibitively expensive: symbols the runtime does not export fail in
     * the ld.so exception path (~10us each, hundreds of microseconds per
     * call in total), which showed up as a ~2x end-to-end slowdown. */
    static uint32_t attempted = 0; /* bit i -> fn index below */
    static const char *const kNames[] = {
        "rtSetDevice",
        "rtGetDeviceInfo",
        "rtGetAiCoreCount",
        "rtKernelLaunch",
        "rtKernelLaunchWithHandle",
        "rtKernelLaunchWithHandleV2",
        "rtKernelLaunchWithFlag",
        "rtKernelLaunchWithFlagV2",
        "rtVectorCoreKernelLaunchWithHandle",
        "rtVectorCoreKernelLaunch",
        "rtKernelLaunchEx",
        "rtKernelLaunchFwk",
        "rtLaunchKernelByFuncHandle",
        "rtLaunchKernelByFuncHandleV2",
        "rtLaunchKernelByFuncHandleV3",
        "rtsLaunchKernelWithDevArgs",
        "rtsLaunchKernelWithConfig",
        "rtsLaunchKernelWithHostArgs",
        "rtLaunchKernelWithArgsArray",
        "rtsGetResInCurrentThread",
    };
    const int n_fns = (int)(sizeof(kNames) / sizeof(kNames[0]));
    void **const fns[] = {
        (void **)&real_rtSetDevice,
        (void **)&real_rtGetDeviceInfo,
        (void **)&real_rtGetAiCoreCount,
        (void **)&real_rtKernelLaunch,
        (void **)&real_rtKernelLaunchWithHandle,
        (void **)&real_rtKernelLaunchWithHandleV2,
        (void **)&real_rtKernelLaunchWithFlag,
        (void **)&real_rtKernelLaunchWithFlagV2,
        (void **)&real_rtVectorCoreKernelLaunchWithHandle,
        (void **)&real_rtVectorCoreKernelLaunch,
        (void **)&real_rtKernelLaunchEx,
        (void **)&real_rtKernelLaunchFwk,
        (void **)&real_rtLaunchKernelByFuncHandle,
        (void **)&real_rtLaunchKernelByFuncHandleV2,
        (void **)&real_rtLaunchKernelByFuncHandleV3,
        (void **)&real_rtsLaunchKernelWithDevArgs,
        (void **)&real_rtsLaunchKernelWithConfig,
        (void **)&real_rtsLaunchKernelWithHostArgs,
        (void **)&real_rtLaunchKernelWithArgsArray,
        (void **)&real_rtsGetResInCurrentThread,
    };

    int runtime_available = (mcs_open_runtime() != NULL);
    if (!runtime_available && !mcs_retry_window_open()) {
        /* Populate common hook table (rt_library_entry[]) for memory hooks */
        load_rt_libraries();
        return;
    }

    for (int i = 0; i < n_fns; i++) {
        if (*fns[i] != NULL || (attempted & (1u << i)) != 0) {
            continue;
        }
        *fns[i] = dlsym(RTLD_NEXT, kNames[i]);
        if (*fns[i] == NULL && runtime_available) {
            *fns[i] = dlsym(g_runtime_handle, kNames[i]);
        }
        if (*fns[i] == NULL) {
            attempted |= (1u << i); /* stop retrying until the next window */
        }
    }

    /* Populate common hook table (rt_library_entry[]) for memory hooks */
    load_rt_libraries();
}
