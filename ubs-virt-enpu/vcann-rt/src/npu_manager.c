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

#include "npu_manager.h"
#include <runtime/rt.h>
#include "acl/acl.h"
#include "common.h"
#include "config.h"
#include "core_limiter.h"
#include "dcmi_wrapper.h"
#include "include/common.h"
#include "mem_limiter.h"
#include "runtime_hook.h"
#include "shm_manager.h"
#include "swap_hook.h"
#include "swap_monitor_thread.h"
#include "utils.h"
#include "vnpu_stats.h"

pthread_once_t once_init = PTHREAD_ONCE_INIT;
pthread_once_t post_init_flag = PTHREAD_ONCE_INIT;
static struct npu_info g_npu_info = {0};

#define SOC_VERSION_SIZE 50

bool is_core_limit(void)
{
    return g_npu_info.is_core_limit;
}

size_t get_mem_request_quota(void)
{
    return g_npu_info.mem_request_quota;
}

size_t get_mem_limit_quota(void)
{
    return g_npu_info.mem_limit_quota;
}

// 当request=limit, 不使能显存超分, 不进行换入换出
bool get_swap_enabled(void)
{
    return g_npu_info.mem_request_quota != g_npu_info.mem_limit_quota;
}

void set_mem_request_quota(size_t mem)
{
    g_npu_info.mem_request_quota = mem;
}

void set_mem_limit_quota(size_t mem)
{
    g_npu_info.mem_limit_quota = mem;
}

uint8_t get_core_limit_quota(void)
{
    return g_npu_info.core_limit_quota;
}

uint32_t get_aicore_num(void)
{
    return g_npu_info.aicore_num;
}

int get_device_id(void)
{
    return g_npu_info.device_id;
}

int get_logic_id(void)
{
    return g_npu_info.logic_id;
}

uint8_t get_soc_version(void)
{
    return g_npu_info.soc_version;
}

uint8_t get_vnpu_id(void)
{
    return g_npu_info.vnpu_id;
}

uint8_t get_vnpu_priority(void)
{
    return g_npu_info.vnpu_priority;
}

char *get_vnpu_shm_id(void)
{
    return g_npu_info.shm_id;
}

int get_card_id(void)
{
    return g_npu_info.card_id;
}

schedule_policy_t get_sched_policy(void)
{
    return g_npu_info.sched_policy;
}

bool check_init_success(void)
{
    return g_npu_info.initialization;
}

int get_mem_used(size_t *used)
{
    if (used == NULL) {
        LOG_ERROR("Failed to get memory usage, input argument used can not be NULL");
        return ENPU_FAIL;
    }

    npu_info *npu = &g_npu_info;
    int rc = enpu_dcmi_get_device_resource_info(npu->logic_id, npu->card_id, npu->device_id, used);
    CHECK_RETURN_ERROR_CODE(rc, "Failed to get device resource info.");
    return ENPU_SUCCESS;
}

int enpu_config_info_init()
{
    CHECK_RETURN_RANGE_INT(config.phy_npu_id, 0, MAX_NPU_ID);
    CHECK_RETURN_RANGE_INT(config.vnpu_id, 0, (MAX_VNPU - 1));

    size_t max_memory_quota = SIZE_MAX / MB_TO_B;
    if (config.memory_request == 0 && config.memory_limit == 0) {
        CHECK_RETURN_RANGE_INT((size_t)config.memory_quota, 1, max_memory_quota);
        g_npu_info.mem_request_quota = (size_t)config.memory_quota * MB_TO_B;
        g_npu_info.mem_limit_quota = (size_t)config.memory_quota * MB_TO_B;
    } else {
        CHECK_RETURN_RANGE_INT((size_t)config.memory_request, 1, max_memory_quota);
        CHECK_RETURN_RANGE_INT((size_t)config.memory_limit, 1, max_memory_quota);
        g_npu_info.mem_request_quota = config.memory_request * MB_TO_B;
        g_npu_info.mem_limit_quota = config.memory_limit * MB_TO_B;
    }

    if (config.scheduling_policy == SCHED_POLICY_FIXED_SHARE || config.scheduling_policy == SCHED_POLICY_ELASTIC) {
        CHECK_RETURN_RANGE_INT(config.aicore_quota, 1, MAX_CORE_QUOTA);

        g_npu_info.core_limit_quota = (uint8_t)config.aicore_quota;
        g_npu_info.is_core_limit = true;
    } else if (config.scheduling_policy == SCHED_POLICY_BEST_EFFORT) {
        g_npu_info.is_core_limit = false;
    } else {
        LOG_ERROR("scheduling policy is illegal, %s = %d, should in range [0, %d]\n", OPTION_SCHEDULING_POLICY,
                  config.scheduling_policy, SCHED_POLICY_BEST_EFFORT);
        return ENPU_FAIL;
    }

    g_npu_info.pnpu_id = config.phy_npu_id;
    g_npu_info.vnpu_id = config.vnpu_id;
    g_npu_info.vnpu_priority = (uint8_t)config.priority;
    g_npu_info.sched_policy = config.scheduling_policy;

    int ret = strcpy_s(g_npu_info.shm_id, sizeof(g_npu_info.shm_id), config.shm_id);
    CHECK_COND_RETURN_ERROR_CODE(ret != 0, "Failed to copy the shm_id from the config to the npu manager.");

    LOG_INFO("Successfully to initialize vnpu device.");
    return ENPU_SUCCESS;
}

int enpu_load_config(void)
{
    int rc = load_config(NPU_CONFIG_PATH);
    CHECK_RETURN_ERROR_CODE(rc, "Failed to initialize npu manager.");
    return enpu_config_info_init();
}

int enpu_soc_init(void)
{
    char socVersion[SOC_VERSION_SIZE] = {0};
    aclError res = RUNTIME_HOOK_CALL(rt_library_entry, rtGetSocVersion, socVersion, SOC_VERSION_SIZE);
    CHECK_COND_RETURN_ERROR_CODE(res != ACL_RT_SUCCESS, "Call rtGetSocVersion fails.");
    LOG_INFO("Get socVersion: %s.", socVersion);

    if (strstr(socVersion, "Ascend950") != NULL) {
        g_npu_info.soc_version = SOC_VERSION_ASCEND_950;
    } else if (strstr(socVersion, "Ascend310") != NULL) {
        g_npu_info.soc_version = SOC_VERSION_ASCEND_310;
    } else {
        g_npu_info.soc_version = SOC_VERSION_ASCEND_910;
    }

    int ret = register_callback(g_npu_info.soc_version);
    CHECK_RETURN_ERROR_CODE(ret, "Failed to register callback.");

    return ENPU_SUCCESS;
}

int enpu_device_init(void)
{
    int card_id = -1;
    int device_id = -1;
    int logic_id = -1;

    int rc = enpu_dcmi_get_card_info(g_npu_info.pnpu_id, &card_id, &device_id, &logic_id, g_npu_info.soc_version);
    CHECK_RETURN_ERROR_CODE(rc, "Failed to get card info by enpu_device_init, err:%d npu:%d", rc, g_npu_info.pnpu_id);

    g_npu_info.card_id = card_id;
    g_npu_info.device_id = device_id;
    g_npu_info.logic_id = logic_id;

    unsigned int aicore_num = 0;
    rc = enpu_dcmi_get_aicore_num(logic_id, card_id, device_id, &aicore_num);
    if (rc == ENPU_SUCCESS && aicore_num > 0) {
        g_npu_info.aicore_num = aicore_num;
    } else {
        g_npu_info.aicore_num = DEFAULT_AICORE_NUM;
        LOG_WARN("Failed to get aicore num (ret=%d), fallback to default %u.", rc, (unsigned)DEFAULT_AICORE_NUM);
    }
    LOG_INFO("Device aicore num = %u.", g_npu_info.aicore_num);
    return ENPU_SUCCESS;
}

static shm_state_t *g_shm_state = NULL;
atomic_bool g_shm_state_init = false;

shm_state_t *get_shm_state(void)
{
    if (!atomic_load(&g_shm_state_init) && g_npu_info.shm_id[0] != '\0') {
        g_shm_state = shm_state_posix_shm_attach(g_npu_info.pnpu_id, g_npu_info.shm_id);
        atomic_store(&g_shm_state_init, true);
    }
    return g_shm_state;
}

static void __enpu_global_init(void)
{
    int rc = enpu_load_config();
    CHECK_COND_RETURN(rc != ENPU_SUCCESS, "Failed to load npu config.");

    rc = enpu_soc_init();
    CHECK_COND_RETURN(rc != ENPU_SUCCESS, "Failed to initialize enpu soc.");

    rc = enpu_device_init();
    CHECK_COND_RETURN(rc != ENPU_SUCCESS, "Failed to initialize enpu device.");

    shm_state_t *state = get_shm_state();
    if (state != NULL) {
        rc = swap_hook_global_init(g_npu_info.vnpu_id);
        if (rc != ENPU_SUCCESS) {
            /* swap 是附加能力: 初始化失败(如 swap buffer 尚未就绪)不阻断原有软切分流程,
             * 仅告警并降级为本进程不参与换入换出 */
            LOG_WARN("Failed to initialize swap hook (ret=%d), swap disabled for this session.", rc);
        }
    }

    rc = memory_limiter_init();
    CHECK_COND_RETURN(rc != ENPU_SUCCESS, "Failed to initialize memory limiter");

    rc = vnpu_stats_init(get_vnpu_shm_id());
    CHECK_ERROR_CODE(rc, "Failed to initialize vnpu stats, utilization statistics will be unavailable");

    rc = aicore_limiter_initialize();
    CHECK_COND_RETURN(rc != ENPU_SUCCESS, "Failed to initialize aicore limiter");

    rc = setenv("ENPU_ENABLE", "True", 1);
    CHECK_COND_RETURN(rc != ENPU_SUCCESS, "Failed to set environment variable");

    g_npu_info.initialization = true;
    LOG_INFO("Successfully to initialize all module.");
}

void enpu_global_init(void)
{
    pthread_once(&once_init, __enpu_global_init);
}

int update_shm_hbm_request(size_t request_quota)
{
    shm_state_t *state = get_shm_state();
    if (state == NULL) {
        return ENPU_FAIL;
    }
    return shm_update_hbm_request(state, g_npu_info.vnpu_id, (uint64_t)request_quota);
}

void __enpu_global_init_post(void)
{
    size_t freeSize = 0;
    size_t totalSize = 0;
    size_t appliedSize = get_mem_request_quota();
    aclError ret = RUNTIME_HOOK_CALL(rt_library_entry, rtMemGetInfoEx, RT_MEMORYINFO_HBM, &freeSize, &totalSize);
    LOG_DEBUG("Call rtMemGetInfoEx return:%d, free HBM size:%zu, total HBM size:%zu, user applied HBM size:%zu.", ret,
              freeSize, totalSize, appliedSize);
    CHECK_COND_LOG_((ret != RT_ERROR_NONE), "Get avaliable HBM size failed! ret:%d, freeSize:%zu, totalSize:%zu.", ret,
                    freeSize, totalSize);
    if (appliedSize > totalSize) {
        LOG_WARN("User request HBM size:%zd is bigger than total HBM size:%zd, now set mem_request_quota to %zd.",
                 appliedSize, totalSize, totalSize);

        shm_state_t *state = get_shm_state();
        if (state == NULL) {
            set_mem_limit_quota(totalSize);
        }
    }

    swap_monitor_thread_t **monitor_thread = swap_monitor_get_thread();
    if (*monitor_thread != NULL) {
        int ret = swap_monitor_thread_start(*monitor_thread);
        if (ret != ENPU_SUCCESS) {
            swap_monitor_thread_destroy(*monitor_thread);
            *monitor_thread = NULL;
        }
    } else {
        LOG_INFO("swap monitor thread is not created, skip starting thread.");
    }
}

void enpu_global_init_post(void)
{
    pthread_once(&post_init_flag, __enpu_global_init_post);
}

size_t get_hbm_request_free(void)
{
    shm_state_t *state = get_shm_state();
    if (state == NULL) {
        return 0;
    }
    return (size_t)shm_get_hbm_request_free(state);
}

size_t get_mem_dynamic_free(void)
{
    shm_state_t *state = get_shm_state();
    if (state == NULL) {
        return 0;
    }
    size_t dynamic_free = shm_get_dynamic_free(state);
    return dynamic_free;
}

int update_shm_used(size_t used)
{
    shm_state_t *state = get_shm_state();
    if (state == NULL) {
        return ENPU_FAIL;
    }
    return shm_update_used(state, g_npu_info.vnpu_id, (uint64_t)used);
}

/* 换出重试上限: 防止换出执行端异常时永久自旋, 超限后按内存不足失败返回 */
#define SWAP_OUT_MAX_RETRY 10

int check_and_swap_out(size_t requested, uint8_t flag)
{
    for (int retry = 0; retry < SWAP_OUT_MAX_RETRY; retry++) {
        size_t freeSize = 0;
        size_t totalSize = 0;
        int ret = RUNTIME_HOOK_CALL(rt_library_entry, rtMemGetInfoEx, RT_MEMORYINFO_HBM, &freeSize, &totalSize);
        LOG_DEBUG("Call rtMemGetInfoEx return:%d, free HBM size:%zu, total HBM size:%zu.", ret, freeSize, totalSize);
        CHECK_COND_RETURN_ERROR_CODE((ret != RT_ERROR_NONE), "Get avaliable HBM size failed! ret:%d.", ret);

        if (requested <= freeSize) {
            return ENPU_SUCCESS;
        }

        shm_state_t *state = get_shm_state();
        bool completed = true;
        do {
            completed = atomic_load(&state->swap_out_cmd.completed);
        } while (!completed);

        atomic_store(&state->swap_out_cmd.action, flag);
        atomic_store(&state->swap_out_cmd.completed, false);

        do {
            completed = atomic_load(&state->swap_out_cmd.completed);
        } while (!completed);
    }

    LOG_ERROR("check_and_swap_out: free HBM still insufficient after %d retries, requested=%zu.", SWAP_OUT_MAX_RETRY,
              requested);
    return ENPU_FAIL;
}
