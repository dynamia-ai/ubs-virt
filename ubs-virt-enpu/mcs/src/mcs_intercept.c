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
 * MCS intercept library - LD_PRELOAD target that intercepts Ascend runtime
 * functions to enforce MCS resource limits and priority scheduling.
 * Symbol resolution lives in mcs_rtapi.c.
 *
 * Kernel launches are NEVER modified: blockDim clamping was removed after
 * proving on CANN 9.1 eager paths that host-side tiling bakes the original
 * blockDim into kernel args, so launching with a smaller blockDim silently
 * skips output tiles (all-NaN model outputs). Launch interposers only do
 * priority bookkeeping (mcs_before_kernel_launch); aicore quota is
 * communicated exclusively through the device-info queries
 * (rtGetAiCoreCount/rtGetDeviceInfo/rtsGetResInCurrentThread), which always
 * report the limited core counts.
 */

#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif

#include <pthread.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <time.h>
#include <unistd.h>

#include "mcs_config.h"
#include "mcs_device.h"
#include "mcs_log.h"
#include "mcs_rtapi.h"
#include "mcs_sched.h"

/* Common module - hook framework and memory limiter */
#include "mem_limiter.h"
#include "runtime_hook.h"

/* Activate memory hooks after MCS init completes */
extern void activate_mem_hooks(void);

/* ---------------------------------------------------------------------------
 * Global state
 * ------------------------------------------------------------------------- */
static mcs_shared_state_t *g_mcs_state = NULL;
static int g_mcs_initialized = 0;
static int g_vnpu_id = 0;

/* ---------------------------------------------------------------------------
 * MCS init
 * ------------------------------------------------------------------------- */

static int mcs_do_init(int32_t dev_id)
{
    if (g_mcs_initialized) {
        return 0;
    }

    (void)log_init();

    /* Load config (override path via MCS_CONFIG env var for testing) */
    const char *config_path = getenv("MCS_CONFIG");
    if (config_path == NULL) {
        config_path = MCS_CONFIG_PATH;
    }
    int ret = mcs_load_config(config_path, &g_mcs_config);
    if (ret != 0) {
        LOG_ERROR("Failed to load config from %s", MCS_CONFIG_PATH);
        fprintf(stderr, "[MCS] init failed: cannot load config from %s\n", config_path);
        return -1;
    }
    g_vnpu_id = g_mcs_config.vnpu_id;

    /* Backstop: g_vnpu_id indexes the shared per-vNPU arrays. Config parsing
     * already enforces [0, MCS_MAX_VNPU); never register with a bad id. */
    if (g_vnpu_id < 0 || g_vnpu_id >= MCS_MAX_VNPU) {
        LOG_ERROR("vNPU id %d out of range [0, %d)", g_vnpu_id, MCS_MAX_VNPU);
        fprintf(stderr, "[MCS] init failed: virtual-npu-id %d out of range [0, %d)\n", g_vnpu_id, MCS_MAX_VNPU);
        return -1;
    }

    /* Set device first so rtGetDeviceInfo works in subprocess context */
    if (real_rtSetDevice != NULL) {
        real_rtSetDevice(dev_id);
    }

    /* Get physical aicore counts from the real runtime.
     * aicube comes from the real rtGetAiCoreCount (per-SoC spec), and aivector
     * from rtGetDeviceInfo, so MCS can compute the quota-limited values. */
    int64_t physical_aicube = 0;
    int64_t physical_aivector = 0;

    if (real_rtGetAiCoreCount != NULL) {
        uint32_t aicube = 0;
        if (real_rtGetAiCoreCount(&aicube) == 0) {
            physical_aicube = (int64_t)aicube;
        }
    }
    if (physical_aicube <= 0 && real_rtGetDeviceInfo != NULL) {
        real_rtGetDeviceInfo(0, MCS_MODULE_TYPE_AICORE, MCS_INFO_TYPE_CORE_NUM, &physical_aicube);
    }
    if (real_rtGetDeviceInfo != NULL) {
        real_rtGetDeviceInfo(0, MCS_MODULE_TYPE_VECTOR_CORE, MCS_INFO_TYPE_CORE_NUM, &physical_aivector);
    }
    if (physical_aicube <= 0) {
        LOG_ERROR("Failed to get physical aicore count");
        fprintf(stderr, "[MCS] init failed: cannot query physical aicore count (rtGetAiCoreCount/rtGetDeviceInfo)\n");
        return -1;
    }
    LOG_INFO("Physical aicube=%ld, aivector=%ld, quota=%d%%", (long)physical_aicube, (long)physical_aivector,
             g_mcs_config.aicore_quota);

    /* Init device limits */
    mcs_device_init((int32_t)physical_aicube, (int32_t)physical_aivector, g_mcs_config.aicore_quota);

    LOG_INFO("Limited aicube=%d, aivector=%d", mcs_get_limited_aicube(), mcs_get_limited_aivector());

    /* Init shared memory for priority scheduling */
    ret = mcs_shm_init(g_mcs_config.shm_id, &g_mcs_state);
    if (ret != 0) {
        LOG_ERROR("Failed to init shared memory");
        fprintf(stderr, "[MCS] init failed: cannot init shared memory '%s'\n", g_mcs_config.shm_id);
        return -1;
    }

    /* Register this vNPU's priority */
    g_mcs_state->vnpu_priority[g_vnpu_id] = g_mcs_config.vnpu_priority;

    LOG_INFO("Memory quota=%d MB", g_mcs_config.memory_quota);

    LOG_INFO("Initialized vNPU %d, priority=%d, limited aicube=%d", g_vnpu_id, g_mcs_config.vnpu_priority,
             mcs_get_limited_aicube());

    g_mcs_initialized = 1;
    activate_mem_hooks();
    return 0;
}

/* ---------------------------------------------------------------------------
 * Intercepted functions (visibility default for LD_PRELOAD)
 * ------------------------------------------------------------------------- */

__attribute__((visibility("default"))) rtError_t rtSetDevice(int32_t devId)
{
    load_real_functions();
    LOG_INFO("rtSetDevice(%d) intercepted - attempting MCS init", devId);
    if (mcs_do_init(devId) != 0) {
        /* Refuse service, like vcann-rt: a broken config (missing keys,
         * invalid aicore-quota, invalid vnpu_priority, shm failure, ...)
         * must NOT degrade into an uncontrolled pass-through - the
         * application sees rtSetDevice fail and cannot use the device
         * without the configured limits. */
        LOG_ERROR("MCS init failed");
        fprintf(stderr,
                "[MCS] FATAL: MCS init failed - refusing rtSetDevice. Fix the config (%s) and "
                "restart the process. See /var/log/enpu/mcs/ for details.\n",
                getenv("MCS_CONFIG") != NULL ? getenv("MCS_CONFIG") : MCS_CONFIG_PATH);
        return -1;
    }
    if (real_rtSetDevice != NULL) {
        return real_rtSetDevice(devId);
    }
    return -1;
}

__attribute__((visibility("default"))) rtError_t rtGetDeviceInfo(uint32_t deviceId, int32_t moduleType,
                                                                 int32_t infoType, int64_t *val)
{
    /* If MCS handles this query, return limited count */
    if (g_mcs_initialized) {
        mcs_rtError_t ret = mcs_rtGetDeviceInfo_hook(deviceId, moduleType, infoType, val);
        if (ret == MCS_RT_ERROR_NONE) {
            return 0;
        }
    }

    /* Otherwise pass through to real function */
    if (real_rtGetDeviceInfo != NULL) {
        return real_rtGetDeviceInfo(deviceId, moduleType, infoType, val);
    }
    return -1;
}

/*
 * Compute the quota-limited aicube count WITHOUT requiring full MCS init.
 * LD_PRELOAD interception is live from process start, so this can run before
 * rtSetDevice/init. It only needs two cheap, side-effect-free things:
 *   - read the config file (idempotent, no shm/priority/memory setup)
 *   - query the real rtGetAiCoreCount once for the physical count
 * and then apply the quota. This ensures the very first Triton
 * get_device_properties query (which may precede rtSetDevice and gets cached
 * via lru_cache) already sees the limited value instead of the static
 * per-SoC spec value.
 */
static uint32_t g_lazy_limited_aicube = 0;

static uint32_t mcs_lazy_limited_aicube(void)
{
    if (g_lazy_limited_aicube > 0) {
        return g_lazy_limited_aicube;
    }
    if (real_rtGetAiCoreCount == NULL) {
        return 0;
    }

    /* Load config lazily if mcs_do_init has not run yet. */
    if (g_mcs_config.aicore_quota <= 0) {
        const char *config_path = getenv("MCS_CONFIG");
        if (config_path == NULL) {
            config_path = MCS_CONFIG_PATH;
        }
        if (mcs_load_config(config_path, &g_mcs_config) != 0) {
            return 0;
        }
    }

    uint32_t physical = 0;
    if (real_rtGetAiCoreCount(&physical) != 0 || physical == 0) {
        return 0;
    }
    g_lazy_limited_aicube = (uint32_t)mcs_calc_limited_aicore((int32_t)physical, g_mcs_config.aicore_quota);
    return g_lazy_limited_aicube;
}

/*
 * Intercept rtGetAiCoreCount and always report the quota-limited aicube
 * count. The real function only returns the static per-SoC spec value, which
 * the user process must not see as available. Works whether or not the full
 * MCS init has completed.
 */
__attribute__((visibility("default"))) rtError_t rtGetAiCoreCount(uint32_t *aiCoreCnt)
{
    load_real_functions();

    if (aiCoreCnt == NULL) {
        return -1;
    }

    if (g_mcs_initialized) {
        *aiCoreCnt = (uint32_t)mcs_get_limited_aicube();
        LOG_DEBUG("rtGetAiCoreCount -> %u (MCS initialized)", *aiCoreCnt);
        return 0;
    }

    /* Full MCS init not done yet: return the limit computed lazily, so an
     * early Triton query does not cache the physical per-SoC value. */
    uint32_t limited = mcs_lazy_limited_aicube();
    if (limited > 0) {
        *aiCoreCnt = limited;
        LOG_DEBUG("rtGetAiCoreCount -> %u (lazy, MCS not initialized)", *aiCoreCnt);
        return 0;
    }

    /* Could not compute a limit (e.g. config error): fall back to the real
     * function so we never break the device query path. */
    if (real_rtGetAiCoreCount != NULL) {
        uint32_t passthrough = 0;
        rtError_t ret = real_rtGetAiCoreCount(&passthrough);
        if (ret == 0) {
            *aiCoreCnt = passthrough;
        }
        LOG_DEBUG("rtGetAiCoreCount -> passthrough %u (could not compute limit)", passthrough);
        return ret;
    }
    return -1;
}

/* Helper: handle priority scheduling before kernel launch.
 * High priority increments the anti-starvation burst counter; normal
 * priority waits while a high-priority vNPU is active AND the burst counter
 * is below the limit (5ms idle rule unchanged), then consumes the
 * entitlement and resets the counter. See mcs_before_launch. */
static inline void mcs_before_kernel_launch(void)
{
    if (!g_mcs_initialized || g_mcs_state == NULL) {
        return;
    }

    mcs_before_launch(g_mcs_state, g_vnpu_id, g_mcs_config.vnpu_priority);
}

/* ---------------------------------------------------------------------------
 * rtKernelLaunch (stubFunc-based)
 * ------------------------------------------------------------------------- */
__attribute__((visibility("default"))) rtError_t rtKernelLaunch(const void *stubFunc, uint32_t blockDim, void *args,
                                                                uint32_t argsSize, void *smDesc, void *stm)
{
    mcs_before_kernel_launch();
    LOG_DEBUG("rtKernelLaunch stub=%p blockDim %u", stubFunc, blockDim);

    if (real_rtKernelLaunch != NULL) {
        return real_rtKernelLaunch(stubFunc, blockDim, args, argsSize, smDesc, stm);
    }
    return -1;
}

/* ---------------------------------------------------------------------------
 * rtKernelLaunchWithHandle (handle-based, with kernelInfo)
 * ------------------------------------------------------------------------- */
__attribute__((visibility("default"))) rtError_t rtKernelLaunchWithHandle(void *hdl, const uint64_t tilingKey,
                                                                          uint32_t blockDim, void *argsInfo,
                                                                          void *smDesc, void *stm,
                                                                          const void *kernelInfo)
{
    mcs_before_kernel_launch();
    LOG_DEBUG("rtKernelLaunchWithHandle hdl=%p tilingKey=%lu blockDim %u -> %u", hdl, (unsigned long)tilingKey,
              blockDim);

    if (real_rtKernelLaunchWithHandle != NULL) {
        return real_rtKernelLaunchWithHandle(hdl, tilingKey, blockDim, argsInfo, smDesc, stm, kernelInfo);
    }
    return -1;
}

/* ---------------------------------------------------------------------------
 * rtKernelLaunchWithHandleV2 (handle-based, with cfgInfo)
 * ------------------------------------------------------------------------- */
__attribute__((visibility("default"))) rtError_t rtKernelLaunchWithHandleV2(void *hdl, const uint64_t tilingKey,
                                                                            uint32_t blockDim, void *argsInfo,
                                                                            void *smDesc, void *stm,
                                                                            const void *cfgInfo)
{
    mcs_before_kernel_launch();
    LOG_DEBUG("rtKernelLaunchWithHandleV2 hdl=%p tilingKey=%lu blockDim %u -> %u", hdl, (unsigned long)tilingKey,
              blockDim);

    if (real_rtKernelLaunchWithHandleV2 != NULL) {
        return real_rtKernelLaunchWithHandleV2(hdl, tilingKey, blockDim, argsInfo, smDesc, stm, cfgInfo);
    }
    return -1;
}

/* ---------------------------------------------------------------------------
 * rtKernelLaunchWithFlag (stubFunc-based, with flags)
 * ------------------------------------------------------------------------- */
__attribute__((visibility("default"))) rtError_t rtKernelLaunchWithFlag(const void *stubFunc, uint32_t blockDim,
                                                                        void *argsInfo, void *smDesc, void *stm,
                                                                        uint32_t flags)
{
    mcs_before_kernel_launch();
    LOG_DEBUG("rtKernelLaunchWithFlag stub=%p blockDim %u", stubFunc, blockDim);

    if (real_rtKernelLaunchWithFlag != NULL) {
        return real_rtKernelLaunchWithFlag(stubFunc, blockDim, argsInfo, smDesc, stm, flags);
    }
    return -1;
}

/* ---------------------------------------------------------------------------
 * rtKernelLaunchWithFlagV2 (stubFunc-based, with flags + cfgInfo)
 * ------------------------------------------------------------------------- */
__attribute__((visibility("default"))) rtError_t rtKernelLaunchWithFlagV2(const void *stubFunc, uint32_t blockDim,
                                                                          void *argsInfo, void *smDesc, void *stm,
                                                                          uint32_t flags, const void *cfgInfo)
{
    mcs_before_kernel_launch();
    LOG_DEBUG("rtKernelLaunchWithFlagV2 stub=%p blockDim %u", stubFunc, blockDim);

    if (real_rtKernelLaunchWithFlagV2 != NULL) {
        return real_rtKernelLaunchWithFlagV2(stubFunc, blockDim, argsInfo, smDesc, stm, flags, cfgInfo);
    }
    return -1;
}

/* ---------------------------------------------------------------------------
 * rtVectorCoreKernelLaunchWithHandle (explicitly vector core)
 * ------------------------------------------------------------------------- */
__attribute__((visibility("default"))) rtError_t rtVectorCoreKernelLaunchWithHandle(void *hdl, const uint64_t tilingKey,
                                                                                    uint32_t blockDim, void *argsInfo,
                                                                                    void *smDesc, void *stm,
                                                                                    const void *cfgInfo)
{
    mcs_before_kernel_launch();
    LOG_DEBUG("rtVectorCoreKernelLaunchWithHandle tilingKey=%lu blockDim %u -> %u", (unsigned long)tilingKey, blockDim);

    if (real_rtVectorCoreKernelLaunchWithHandle != NULL) {
        return real_rtVectorCoreKernelLaunchWithHandle(hdl, tilingKey, blockDim, argsInfo, smDesc, stm, cfgInfo);
    }
    return -1;
}

/* ---------------------------------------------------------------------------
 * rtVectorCoreKernelLaunch (explicitly vector core)
 * ------------------------------------------------------------------------- */
__attribute__((visibility("default"))) rtError_t rtVectorCoreKernelLaunch(const void *stubFunc, uint32_t blockDim,
                                                                          void *argsInfo, void *smDesc, void *stm,
                                                                          uint32_t flags, const void *cfgInfo)
{
    mcs_before_kernel_launch();
    LOG_DEBUG("rtVectorCoreKernelLaunch stub=%p blockDim %u", stubFunc, blockDim);

    if (real_rtVectorCoreKernelLaunch != NULL) {
        return real_rtVectorCoreKernelLaunch(stubFunc, blockDim, argsInfo, smDesc, stm, flags, cfgInfo);
    }
    return -1;
}

/* ---------------------------------------------------------------------------
 * rtKernelLaunchEx (framework launch, blockDim embedded in args blob)
 * Only observed/passed through; blockDim is not a direct parameter.
 * ------------------------------------------------------------------------- */
__attribute__((visibility("default"))) rtError_t rtKernelLaunchEx(void *args, uint32_t argsSize, uint32_t flags,
                                                                  void *stm)
{
    mcs_before_kernel_launch();
    LOG_DEBUG("rtKernelLaunchEx args=%p argsSize=%u flags=%u", args, argsSize, flags);

    if (real_rtKernelLaunchEx != NULL) {
        return real_rtKernelLaunchEx(args, argsSize, flags, stm);
    }
    return -1;
}

/* ---------------------------------------------------------------------------
 * rtKernelLaunchFwk (framework launch, opName + args blob)
 * Only observed/passed through; blockDim is not a direct parameter.
 * ------------------------------------------------------------------------- */
__attribute__((visibility("default"))) rtError_t rtKernelLaunchFwk(const char *opName, void *args, uint32_t argsSize,
                                                                   uint32_t flags, void *stm)
{
    mcs_before_kernel_launch();
    LOG_DEBUG("rtKernelLaunchFwk op=%s args=%p argsSize=%u flags=%u", opName ? opName : "(null)", args, argsSize,
              flags);

    if (real_rtKernelLaunchFwk != NULL) {
        return real_rtKernelLaunchFwk(opName, args, argsSize, flags, stm);
    }
    return -1;
}

/* ---------------------------------------------------------------------------
 * rtLaunchKernelByFuncHandle (funcHandle-based, explicit numBlocks)
 * ------------------------------------------------------------------------- */
__attribute__((visibility("default"))) rtError_t rtLaunchKernelByFuncHandle(void *funcHandle, uint32_t numBlocks,
                                                                            void *argsHandle, void *stm)
{
    mcs_before_kernel_launch();
    LOG_DEBUG("rtLaunchKernelByFuncHandle numBlocks %u", numBlocks);

    if (real_rtLaunchKernelByFuncHandle != NULL) {
        return real_rtLaunchKernelByFuncHandle(funcHandle, numBlocks, argsHandle, stm);
    }
    return -1;
}

/* ---------------------------------------------------------------------------
 * rtLaunchKernelByFuncHandleV2 (funcHandle-based, explicit numBlocks)
 * ------------------------------------------------------------------------- */
__attribute__((visibility("default"))) rtError_t rtLaunchKernelByFuncHandleV2(void *funcHandle, uint32_t numBlocks,
                                                                              void *argsHandle, void *stm,
                                                                              const void *cfgInfo)
{
    mcs_before_kernel_launch();
    LOG_DEBUG("rtLaunchKernelByFuncHandleV2 numBlocks %u", numBlocks);

    if (real_rtLaunchKernelByFuncHandleV2 != NULL) {
        return real_rtLaunchKernelByFuncHandleV2(funcHandle, numBlocks, argsHandle, stm, cfgInfo);
    }
    return -1;
}

/* ---------------------------------------------------------------------------
 * rtLaunchKernelByFuncHandleV3 (funcHandle-based, explicit numBlocks)
 * ------------------------------------------------------------------------- */
__attribute__((visibility("default"))) rtError_t rtLaunchKernelByFuncHandleV3(void *funcHandle, uint32_t numBlocks,
                                                                              const void *argsInfo, void *stm,
                                                                              const void *cfgInfo)
{
    mcs_before_kernel_launch();
    LOG_DEBUG("rtLaunchKernelByFuncHandleV3 numBlocks %u", numBlocks);

    if (real_rtLaunchKernelByFuncHandleV3 != NULL) {
        return real_rtLaunchKernelByFuncHandleV3(funcHandle, numBlocks, argsInfo, stm, cfgInfo);
    }
    return -1;
}

/* ---------------------------------------------------------------------------
 * rtsLaunchKernelWithDevArgs (funcHandle-based, explicit numBlocks + dev args)
 * ------------------------------------------------------------------------- */
__attribute__((visibility("default"))) rtError_t rtsLaunchKernelWithDevArgs(void *funcHandle, uint32_t numBlocks,
                                                                            void *stm, void *cfg, const void *args,
                                                                            uint32_t argsSize, void *reserve)
{
    mcs_before_kernel_launch();
    LOG_DEBUG("rtsLaunchKernelWithDevArgs numBlocks %u", numBlocks);

    if (real_rtsLaunchKernelWithDevArgs != NULL) {
        return real_rtsLaunchKernelWithDevArgs(funcHandle, numBlocks, stm, cfg, args, argsSize, reserve);
    }
    return -1;
}

/* ---------------------------------------------------------------------------
 * rtsLaunchKernelWithConfig (funcHandle-based, explicit numBlocks + args handle)
 * ------------------------------------------------------------------------- */
__attribute__((visibility("default"))) rtError_t rtsLaunchKernelWithConfig(void *funcHandle, uint32_t numBlocks,
                                                                           void *stm, void *cfg, void *argsHandle,
                                                                           void *reserve)
{
    mcs_before_kernel_launch();
    LOG_DEBUG("rtsLaunchKernelWithConfig numBlocks %u", numBlocks);

    if (real_rtsLaunchKernelWithConfig != NULL) {
        return real_rtsLaunchKernelWithConfig(funcHandle, numBlocks, stm, cfg, argsHandle, reserve);
    }
    return -1;
}

/* ---------------------------------------------------------------------------
 * rtsLaunchKernelWithHostArgs (funcHandle-based, explicit numBlocks + host args)
 * ------------------------------------------------------------------------- */
__attribute__((visibility("default"))) rtError_t rtsLaunchKernelWithHostArgs(void *funcHandle, uint32_t numBlocks,
                                                                             void *stm, void *cfg, void *hostArgs,
                                                                             uint32_t argsSize, void *placeHolderArray,
                                                                             uint32_t placeHolderNum)
{
    mcs_before_kernel_launch();

    /* Clamp + probe are fused in the helper: the kernel-type probe only runs
     * when a clamp could actually trigger (numBlocks above the per-type
     * limit) and its result - success or failure - is cached per funcHandle.
     * Probing on every launch costs real per-op overhead and previously
     * starved the device. */
    LOG_DEBUG("rtsHostArgs func=%p numBlocks %u", funcHandle, numBlocks);

    if (real_rtsLaunchKernelWithHostArgs != NULL) {
        return real_rtsLaunchKernelWithHostArgs(funcHandle, numBlocks, stm, cfg, hostArgs, argsSize, placeHolderArray,
                                                placeHolderNum);
    }
    return -1;
}

/* ---------------------------------------------------------------------------
 * rtLaunchKernelWithArgsArray (function + explicit numBlocks + args array)
 * ------------------------------------------------------------------------- */
__attribute__((visibility("default"))) rtError_t rtLaunchKernelWithArgsArray(void *func, uint32_t numBlocks, void *stm,
                                                                             void *cfg, void **args)
{
    mcs_before_kernel_launch();
    LOG_DEBUG("rtLaunchKernelWithArgsArray numBlocks %u", numBlocks);

    if (real_rtLaunchKernelWithArgsArray != NULL) {
        return real_rtLaunchKernelWithArgsArray(func, numBlocks, stm, cfg, args);
    }
    return -1;
}

/* ---------------------------------------------------------------------------
 * rtsGetResInCurrentThread (per-thread Cube/Vector Core resource limit query)
 * Report the quota-limited counts instead of the physical core counts:
 *   RT_DEV_RES_CUBE_CORE   (=0) -> limited aicube
 *   RT_DEV_RES_VECTOR_CORE (=1) -> limited aivector
 * ------------------------------------------------------------------------- */
__attribute__((visibility("default"))) rtError_t rtsGetResInCurrentThread(int32_t type, uint32_t *value)
{
    load_real_functions();

    if (value == NULL) {
        return -1;
    }

    if (g_mcs_initialized && type >= 0 && type < RT_DEV_RES_TYPE_MAX) {
        if (type == RT_DEV_RES_CUBE_CORE) {
            *value = (uint32_t)mcs_get_limited_aicube();
        } else {
            *value = (uint32_t)mcs_get_limited_aivector();
        }
        LOG_DEBUG("rtsGetResInCurrentThread type=%d -> %u (limited_aicube=%d, limited_aivector=%d)", type, *value,
                  mcs_get_limited_aicube(), mcs_get_limited_aivector());
        return 0;
    }

    /* MCS not initialized or unknown type: pass through to the real runtime */
    if (real_rtsGetResInCurrentThread != NULL) {
        return real_rtsGetResInCurrentThread(type, value);
    }
    return -1;
}
