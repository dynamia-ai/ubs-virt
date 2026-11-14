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

#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif
#include <dlfcn.h>
#include "core_limiter.h"
#include "log.h"
#include "npu_manager.h"
#include "runtime_hook.h"

pthread_once_t pre_rt_init_flag = PTHREAD_ONCE_INIT;

void load_rt_libraries(void)
{
    int i;
    for (i = 0; i < RUNTIME_ENTRY_END; i++) {
        rt_library_entry[i].func_ptr = dlsym(RTLD_NEXT, rt_library_entry[i].name);
        if (rt_library_entry[i].func_ptr == NULL) {
            // cann-9.2.0新增特有接口，cann-9.2.0开始需要增加aclrtImpl接口拦截，故以此作为aclrt_impl_hook开关标识
            if (strcmp(rt_library_entry[i].name, "aclrtLaunchSIMTKernelWithHostArgsImpl") == 0) {
                set_aclrt_impl_hook_enable(false);
            }
            if (strcmp(rt_library_entry[i].name, "rtModelGetStreams") == 0) {
                set_vnpu_stats_enable(false);
                LOG_WARN("[VNPU-STATS] Duration sampling disabled for all launch types: "
                         "rtModelGetStreams is unavailable; block counters remain enabled.");
            }
            LOG_WARN("Can not find function %s, because the runtime version you are using is different "
                     "from our preset version.",
                     rt_library_entry[i].name);
        }
    }
    LOG_INFO("[VNPU-STATS] Runtime capabilities: duration_sampling=%d aclrt_impl_hook=%d.", get_vnpu_stats_enable(),
             get_aclrt_impl_hook_enable());
    return;
}

void pre_rt_init(void)
{
    pthread_once(&pre_rt_init_flag, load_rt_libraries);
}

RUNTIME_HOOK_DEFINE(rtSetDevice, int32_t devId)
{
    int res = log_init();
    CHECK_COND_RETURN_((res != ENPU_SUCCESS), res, "Failed to init log module, res:%d.", res);
    pre_rt_init();
    enpu_global_init();
    CHECK_COND_RETURN_(!check_init_success(), ACL_ERROR_UNINITIALIZE,
                       "Failed to initialize vcann-rt, please check the config file in %s.", NPU_CONFIG_PATH);
    if (devId != 0) {
        LOG_WARN("SetDevice should only pass devId=0.");
    }

    LOG_DEBUG("Hook init rtSetDevice devId:%" PRIi32 ".", devId);
    LOG_DEBUG("The total time slice length is: %zd, and %zd %% of it is available.", VNPU_SCHEULE_PERIOD / NS_PER_MS,
              get_core_limit_quota());
    aclError ret = RUNTIME_HOOK_CALL(rt_library_entry, rtSetDevice, devId);
    CHECK_COND_RETURN_((ret != ACL_RT_SUCCESS), ret, "Call rtSetDevice fails, ret:%d.", ret);
    enpu_global_init_post();
    return ACL_RT_SUCCESS;
}

RUNTIME_HOOK_DEFINE(aclrtSetDeviceImpl, int32_t devId)
{
    if (!get_aclrt_impl_hook_enable()) {
        return RUNTIME_HOOK_CALL(rt_library_entry, aclrtSetDeviceImpl, devId);
    }
    int res = log_init();
    CHECK_COND_RETURN_((res != ENPU_SUCCESS), res, "Failed to init log module, res:%d.", res);
    pre_rt_init();
    enpu_global_init();
    CHECK_COND_RETURN_(!check_init_success(), ACL_ERROR_UNINITIALIZE,
                       "Failed to initialize vcann-rt, please check the config file in %s.", NPU_CONFIG_PATH);
    if (devId != 0) {
        LOG_WARN("SetDevice should only pass devId=0.");
    }

    LOG_DEBUG("Hook init aclrtSetDeviceImpl devId:%" PRIi32 ".", devId);
    LOG_DEBUG("The total time slice length is: %zd, and %zd %% of it is available.", VNPU_SCHEULE_PERIOD / NS_PER_MS,
              get_core_limit_quota());
    aclError ret = RUNTIME_HOOK_CALL(rt_library_entry, aclrtSetDeviceImpl, devId);
    CHECK_COND_RETURN_((ret != ACL_RT_SUCCESS), ret, "Call aclrtSetDeviceImpl fails, ret:%d.", ret);
    enpu_global_init_post();
    return ACL_RT_SUCCESS;
}

RUNTIME_HOOK_DEFINE(rtSetDeviceEx, int32_t devId)
{
    int res = log_init();
    CHECK_COND_RETURN_((res != ENPU_SUCCESS), res, "Failed to init log module, res:%d.", res);
    pre_rt_init();
    enpu_global_init();
    CHECK_COND_RETURN_(!check_init_success(), ACL_ERROR_UNINITIALIZE,
                       "Failed to initialize vcann-rt, please check the config file in %s.", NPU_CONFIG_PATH);
    if (devId != 0) {
        LOG_WARN("SetDevice should only pass devId=0.");
    }

    LOG_DEBUG("Hook init rtSetDeviceEx devId:%" PRIi32 ".", devId);
    LOG_DEBUG("The total time slice length is: %zd, and %zd %% of it is available.", VNPU_SCHEULE_PERIOD / NS_PER_MS,
              get_core_limit_quota());
    aclError ret = RUNTIME_HOOK_CALL(rt_library_entry, rtSetDeviceEx, devId);
    CHECK_COND_RETURN_((ret != ACL_RT_SUCCESS), ret, "Call rtSetDeviceEx fails, ret:%d.", ret);
    enpu_global_init_post();
    return ACL_RT_SUCCESS;
}

RUNTIME_HOOK_DEFINE(rtSetDeviceWithFlags, int32_t devId, uint64_t flags)
{
    int res = log_init();
    CHECK_COND_RETURN_((res != ENPU_SUCCESS), res, "Failed to init log module, res:%d.", res);
    pre_rt_init();
    enpu_global_init();
    CHECK_COND_RETURN_(!check_init_success(), ACL_ERROR_UNINITIALIZE,
                       "Failed to initialize vcann-rt, please check the config file in %s.", NPU_CONFIG_PATH);
    if (devId != 0) {
        LOG_WARN("SetDevice should only pass devId=0.");
    }

    LOG_DEBUG("Hook init rtSetDeviceWithFlags devId:%" PRIi32 ".", devId);
    LOG_DEBUG("The total time slice length is: %zd, and %zd %% of it is available.", VNPU_SCHEULE_PERIOD / NS_PER_MS,
              get_core_limit_quota());
    aclError ret = RUNTIME_HOOK_CALL(rt_library_entry, rtSetDeviceWithFlags, devId, flags);
    CHECK_COND_RETURN_((ret != ACL_RT_SUCCESS), ret, "Call rtSetDeviceWithFlags fails, ret:%d.", ret);
    enpu_global_init_post();
    return ACL_RT_SUCCESS;
}

RUNTIME_HOOK_DEFINE(rtSetDeviceWithoutTsd, int32_t devId)
{
    int res = log_init();
    CHECK_COND_RETURN_((res != ENPU_SUCCESS), res, "Failed to init log module, res:%d.", res);
    pre_rt_init();
    enpu_global_init();
    CHECK_COND_RETURN_(!check_init_success(), ACL_ERROR_UNINITIALIZE,
                       "Failed to initialize vcann-rt, please check the config file in %s.", NPU_CONFIG_PATH);
    if (devId != 0) {
        LOG_WARN("SetDevice should only pass devId=0.");
    }

    LOG_DEBUG("Hook init rtSetDeviceWithoutTsd devId:%" PRIi32 ".", devId);
    LOG_DEBUG("The total time slice length is: %zd, and %zd %% of it is available.", VNPU_SCHEULE_PERIOD / NS_PER_MS,
              get_core_limit_quota());
    aclError ret = RUNTIME_HOOK_CALL(rt_library_entry, rtSetDeviceWithoutTsd, devId);
    CHECK_COND_RETURN_((ret != ACL_RT_SUCCESS), ret, "Call rtSetDeviceWithoutTsd fails, ret:%d.", ret);
    enpu_global_init_post();
    return ACL_RT_SUCCESS;
}

RUNTIME_HOOK_DEFINE(aclrtSetDeviceWithoutTsdVXXImpl, int32_t devId)
{
    if (!get_aclrt_impl_hook_enable()) {
        return RUNTIME_HOOK_CALL(rt_library_entry, aclrtSetDeviceWithoutTsdVXXImpl, devId);
    }
    int res = log_init();
    CHECK_COND_RETURN_((res != ENPU_SUCCESS), res, "Failed to init log module, res:%d.", res);
    pre_rt_init();
    enpu_global_init();
    CHECK_COND_RETURN_(!check_init_success(), ACL_ERROR_UNINITIALIZE,
                       "Failed to initialize vcann-rt, please check the config file in %s.", NPU_CONFIG_PATH);
    if (devId != 0) {
        LOG_WARN("SetDevice should only pass devId=0.");
    }

    LOG_DEBUG("Hook init aclrtSetDeviceWithoutTsdVXXImpl devId:%" PRIi32 ".", devId);
    LOG_DEBUG("The total time slice length is: %zd, and %zd %% of it is available.", VNPU_SCHEULE_PERIOD / NS_PER_MS,
              get_core_limit_quota());
    aclError ret = RUNTIME_HOOK_CALL(rt_library_entry, aclrtSetDeviceWithoutTsdVXXImpl, devId);
    CHECK_COND_RETURN_((ret != ACL_RT_SUCCESS), ret, "Call aclrtSetDeviceWithoutTsdVXXImpl fails, ret:%d.", ret);
    enpu_global_init_post();
    return ACL_RT_SUCCESS;
}
