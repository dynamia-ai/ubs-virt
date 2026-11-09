/*
 * Copyright (c) Huawei Technologies Co., Ltd. 2025-2026. All rights reserved.
 * enpu-manager is licensed under Mulan PSL v2.
 */

#include "enpu_manager.h"
#include <pthread.h>
#include <stdatomic.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#include "../internal/npu_allocator.h"
#include "allocation.h"
#include "common.h"
#include "config_manager.h"
#include "dcmi_wrapper.h"
#include "enpu_config.h"
#include "log.h"
#include "pod_watchdog.h"
#include "securec.h"
#include "shm_manager.h"
#include "swap_buffer_manager.h"
#include "swap_resolver.h"
#include "swap_resolver_thread.h"

#define SWAP_RESOLVER_POLL_INTERVAL_MS 10

struct enpu_manager {
    npu_allocator_t *allocator;
    bool swap_enabled;
    swap_buffer_manager_t *swap_buf_mgr;
    swap_resolver_t *swap_resolver;
    swap_resolver_thread_t *swap_resolver_threads[MAX_NPU_PER_NODE];
    int swap_resolver_thread_count;
    pod_watchdog_t *watchdog;
    pthread_t watchdog_thread;
    atomic_bool running;
    atomic_bool watchdog_running;
    uint64_t watchdog_poll_interval_ms;
};

#define CHECK_NULL_RET(param, ret) \
    do {                           \
        if ((param) == NULL) {     \
            return (ret);          \
        }                          \
    } while (0)

typedef struct {
    swap_status_response_t *resp;
} swapped_vnpu_collector_ctx_t;

static void collect_swapped_vnpu_for_status(int phy_id, int vnpu_id, uint64_t swap_size, void *user_data)
{
    swapped_vnpu_collector_ctx_t *ctx = (swapped_vnpu_collector_ctx_t *)user_data;
    if (ctx->resp->swapped_count < MAX_VNPU_PER_DIE * MAX_NPU_PER_NODE) {
        swapped_model_info_t *info = &ctx->resp->swapped_models[ctx->resp->swapped_count++];
        info->vnpu_id = vnpu_id;
        info->die_id = phy_id;
        info->swap_size = swap_size;
    }
}

static void *watchdog_thread_func(void *arg)
{
    enpu_manager_t *mgr = (enpu_manager_t *)arg;

    LOG_INFO("[ENPU-MGR] Watchdog thread started");

    while (atomic_load(&mgr->watchdog_running)) {
        pod_watchdog_poll_and_cleanup(mgr->watchdog);
        struct timespec ts = {
            .tv_sec = (time_t)(mgr->watchdog_poll_interval_ms / 1000),
            .tv_nsec = (long)((mgr->watchdog_poll_interval_ms % 1000) * 1000000ULL),
        };
        nanosleep(&ts, NULL);
    }

    LOG_INFO("[ENPU-MGR] Watchdog thread stopped");
    return NULL;
}

static int find_pod_allocation(enpu_manager_t *mgr, const char *pod_uid, int *phy_id, int *vnpu_id);
static int enpu_manager_swap_clean_impl(enpu_manager_t *mgr, int phy_id, int vnpu_id);
static int enpu_manager_swap_force_out_impl(enpu_manager_t *mgr, int phy_id, int vnpu_id, const char *pod_uid);
static int enpu_manager_swap_force_in_impl(enpu_manager_t *mgr, int phy_id, int vnpu_id, const char *pod_uid);

int enpu_manager_destroy(enpu_manager_t *mgr);

static void enpu_manager_copy_str(char *dst, size_t dst_len, const char *src)
{
    if (dst == NULL || dst_len == 0 || src == NULL) {
        return;
    }
    int ret = strncpy_s(dst, dst_len, src, strnlen(src, dst_len - 1));
    if (ret != 0) {
        LOG_ERROR("[ENPU-MGR] strncpy_s failed, ret=%d", ret);
    }
}

static int enpu_manager_init_allocator(enpu_manager_t *mgr, const enpu_manager_config_t *config)
{
    allocator_config_t alloc_config = {0};
    if (config != NULL) {
        enpu_manager_copy_str(alloc_config.config_base_path, sizeof(alloc_config.config_base_path),
                              config->config_base_path);
        enpu_manager_copy_str(alloc_config.checkpoint_path, sizeof(alloc_config.checkpoint_path),
                              config->checkpoint_path);
        enpu_manager_copy_str(alloc_config.evaluator_name, sizeof(alloc_config.evaluator_name), config->evaluator_name);
        enpu_manager_copy_str(alloc_config.share_strategy, sizeof(alloc_config.share_strategy), config->share_strategy);
        alloc_config.use_dcmi_stub = config->use_dcmi_stub;
        alloc_config.dcmi_stub = config->dcmi_stub;
    } else {
        enpu_manager_copy_str(alloc_config.config_base_path, sizeof(alloc_config.config_base_path), CONFIG_BASE_PATH);
        enpu_manager_copy_str(alloc_config.checkpoint_path, sizeof(alloc_config.checkpoint_path),
                              DEFAULT_CHECKPOINT_PATH);
        enpu_manager_copy_str(alloc_config.evaluator_name, sizeof(alloc_config.evaluator_name), "share");
        alloc_config.use_dcmi_stub = false;
    }

    mgr->allocator = npu_allocator_create(&alloc_config);
    if (mgr->allocator == NULL) {
        LOG_ERROR("[ENPU-MGR] Failed to create allocator");
        return ENPU_FAIL;
    }

    int ret = npu_allocator_init(mgr->allocator);
    if (ret != ENPU_SUCCESS) {
        LOG_ERROR("[ENPU-MGR] Failed to init allocator: %d", ret);
        npu_allocator_destroy(mgr->allocator);
        mgr->allocator = NULL;
        return ret;
    }

    return ENPU_SUCCESS;
}

static uint64_t enpu_manager_calc_swap_buf(enpu_manager_t *mgr, const enpu_manager_config_t *config,
                                           const uint64_t *die_hbm_mb, bool *any_swap_enabled)
{
    const double mb_to_bytes = (double)(1024ULL * 1024ULL);
    uint64_t swap_buf_size = 0;
    *any_swap_enabled = false;

    for (int i = 0; i < MAX_NPU_PER_NODE; i++) {
        double ratio = (config != NULL) ? config->oversub_ratio[i] : 0.0;
        npu_allocator_set_oversub_ratio(mgr->allocator, i, ratio);
        if (ratio > 0.0 && die_hbm_mb[i] > 0) {
            swap_buf_size += (uint64_t)((1.0 + ratio) * (double)die_hbm_mb[i] * mb_to_bytes);
            *any_swap_enabled = true;
        }
    }

    mgr->swap_enabled = *any_swap_enabled;
    LOG_INFO("[ENPU-MGR] swap_enabled=%d, swap_buffer_size=%lu bytes", *any_swap_enabled ? 1 : 0,
             (unsigned long)swap_buf_size);
    return swap_buf_size;
}

static void enpu_manager_log_swap_config(enpu_manager_t *mgr, const uint64_t *die_hbm_mb)
{
    for (int i = 0; i < MAX_NPU_PER_NODE; i++) {
        if (die_hbm_mb[i] > 0) {
            double ratio = npu_allocator_get_oversub_ratio(mgr->allocator, i);
            LOG_INFO("[ENPU-MGR]   phy=%d  die_hbm=%lu MB  oversub_ratio=%.2f  -> %s", i, (unsigned long)die_hbm_mb[i],
                     ratio, (ratio > 0.0) ? "swap-on" : "swap-off");
        }
    }
}

static int enpu_manager_check_host_mem(uint64_t swap_buf_size, bool any_swap_enabled)
{
    if (!any_swap_enabled) {
        return ENPU_SUCCESS;
    }

    uint64_t mem_free_bytes = 0;
    FILE *fp = fopen("/proc/meminfo", "r");
    if (fp != NULL) {
        char line[256];
        while (fgets(line, sizeof(line), fp)) {
            unsigned long mem_free_kb = 0;
            if (sscanf_s(line, "MemFree: %lu kB", &mem_free_kb) == 1) {
                mem_free_bytes = (uint64_t)mem_free_kb * 1024ULL;
                break;
            }
        }
        fclose(fp);
    }

    if (mem_free_bytes == 0) {
        LOG_WARN("[ENPU-MGR] WARNING: failed to read MemFree from /proc/meminfo, skipping oversub sanity check");
    } else if (swap_buf_size > mem_free_bytes) {
        LOG_ERROR("[ENPU-MGR] configured oversub-ratio too large: required swap buffer %lu bytes > host MemFree "
                  "%lu bytes, aborting",
                  (unsigned long)swap_buf_size, (unsigned long)mem_free_bytes);
        return ENPU_FAIL;
    }
    return ENPU_SUCCESS;
}

static int enpu_manager_init_swap(enpu_manager_t *mgr, const enpu_manager_config_t *config, uint64_t swap_buf_size)
{
    mgr->swap_buf_mgr = shm_swap_buffer_init(swap_buf_size);
    if (mgr->swap_buf_mgr == NULL) {
        LOG_ERROR("[ENPU-MGR] Failed to create swap buffer manager");
        return ENPU_FAIL;
    }

    int pre_wm = (config != NULL && config->swap_pre_watermark > 0) ? config->swap_pre_watermark : 80;
    double pre_threshold = pre_wm / 100.0;
    mgr->swap_resolver = swap_resolver_create(mgr->allocator, mgr->swap_buf_mgr, pre_threshold, 30ULL * 1000000000ULL);
    if (mgr->swap_resolver == NULL) {
        LOG_ERROR("[ENPU-MGR] Failed to create swap resolver");
        shm_swap_buffer_deinit(mgr->swap_buf_mgr);
        mgr->swap_buf_mgr = NULL;
        return ENPU_FAIL;
    }

    shm_manager_t *shm_mgr = npu_allocator_get_shm_manager(mgr->allocator);

    int device_count = 0;
    if (dcmi_get_device_count(&device_count) != ENPU_SUCCESS || device_count <= 0) {
        LOG_WARN("[ENPU-MGR] Failed to get device count for swap_resolver_thread, fallback to MAX_NPU_PER_NODE");
        device_count = MAX_NPU_PER_NODE;
    }
    if (device_count > MAX_NPU_PER_NODE) {
        device_count = MAX_NPU_PER_NODE;
    }

    mgr->swap_resolver_thread_count = 0;
    for (int phy_id = 0; phy_id < device_count; phy_id++) {
        /* 只为启用了 swap 的 die 起 swap_resolver_thread */
        if (npu_allocator_get_oversub_ratio(mgr->allocator, phy_id) <= 0.0) {
            continue;
        }
        swap_resolver_thread_t *srt =
            swap_resolver_thread_create(mgr->swap_resolver, phy_id, SWAP_RESOLVER_POLL_INTERVAL_MS);
        if (srt == NULL) {
            LOG_WARN("[ENPU-MGR] Failed to create swap_resolver_thread for phy_id=%d, swap on this die disabled",
                     phy_id);
            continue;
        }
        mgr->swap_resolver_threads[mgr->swap_resolver_thread_count++] = srt;
    }

    mgr->watchdog = pod_watchdog_create(shm_mgr, mgr->swap_buf_mgr, mgr->allocator);
    if (mgr->watchdog == NULL) {
        LOG_ERROR("[ENPU-MGR] Failed to create watchdog");
        return ENPU_FAIL;
    }

    return ENPU_SUCCESS;
}

enpu_manager_t *enpu_manager_create(const enpu_manager_config_t *config)
{
    enpu_manager_t *mgr = (enpu_manager_t *)calloc(1, sizeof(enpu_manager_t));
    if (mgr == NULL) {
        LOG_ERROR("[ENPU-MGR] Failed to allocate manager memory");
        return NULL;
    }

    if (enpu_manager_init_allocator(mgr, config) != ENPU_SUCCESS) {
        enpu_manager_destroy(mgr);
        return NULL;
    }

    uint64_t die_hbm_mb[MAX_NPU_PER_NODE] = {0};
    if (npu_allocator_get_per_die_hbm_mb(mgr->allocator, die_hbm_mb, MAX_NPU_PER_NODE) != ENPU_SUCCESS) {
        LOG_WARN("[ENPU-MGR] Warning: failed to get per-die HBM, swap may be under-sized");
    }

    bool any_swap_enabled = false;
    uint64_t swap_buf_size = enpu_manager_calc_swap_buf(mgr, config, die_hbm_mb, &any_swap_enabled);
    enpu_manager_log_swap_config(mgr, die_hbm_mb);

    if (enpu_manager_check_host_mem(swap_buf_size, any_swap_enabled) != ENPU_SUCCESS) {
        enpu_manager_destroy(mgr);
        return NULL;
    }

    if (any_swap_enabled && enpu_manager_init_swap(mgr, config, swap_buf_size) != ENPU_SUCCESS) {
        enpu_manager_destroy(mgr);
        return NULL;
    }

    mgr->watchdog_poll_interval_ms =
        (config != NULL && config->watchdog_poll_interval_ms > 0) ? config->watchdog_poll_interval_ms : 500;

    atomic_store(&mgr->running, false);
    atomic_store(&mgr->watchdog_running, false);

    LOG_INFO("[ENPU-MGR] Manager created and initialized");
    return mgr;
}

int enpu_manager_destroy(enpu_manager_t *mgr)
{
    CHECK_NULL_RET(mgr, ENPU_INVALID_PARAM);

    if (mgr->watchdog != NULL) {
        pod_watchdog_destroy(mgr->watchdog);
        mgr->watchdog = NULL;
    }

    for (int i = 0; i < mgr->swap_resolver_thread_count; i++) {
        swap_resolver_thread_destroy(mgr->swap_resolver_threads[i]);
        mgr->swap_resolver_threads[i] = NULL;
    }
    mgr->swap_resolver_thread_count = 0;

    if (mgr->swap_resolver != NULL) {
        swap_resolver_destroy(mgr->swap_resolver);
        mgr->swap_resolver = NULL;
    }

    if (mgr->swap_buf_mgr != NULL) {
        shm_swap_buffer_deinit(mgr->swap_buf_mgr);
        mgr->swap_buf_mgr = NULL;
    }

    if (mgr->allocator != NULL) {
        npu_allocator_destroy(mgr->allocator);
        mgr->allocator = NULL;
    }

    free(mgr);

    LOG_INFO("[ENPU-MGR] Manager destroyed");
    return ENPU_SUCCESS;
}

int enpu_manager_start(enpu_manager_t *mgr)
{
    CHECK_NULL_RET(mgr, ENPU_INVALID_PARAM);

    atomic_store(&mgr->running, true);

    if (mgr->watchdog != NULL) {
        atomic_store(&mgr->watchdog_running, true);
        int wd_ret = pthread_create(&mgr->watchdog_thread, NULL, watchdog_thread_func, mgr);
        if (wd_ret != 0) {
            LOG_ERROR("[ENPU-MGR] Failed to create watchdog thread: %d", wd_ret);
            atomic_store(&mgr->watchdog_running, false);
            atomic_store(&mgr->running, false);
            return ENPU_FAIL;
        }
    }

    // 每个phy_id对应一个swap_resolver_thread（swap 关闭时 thread_count=0，循环为空）
    for (int i = 0; i < mgr->swap_resolver_thread_count; i++) {
        int sret = swap_resolver_thread_start(mgr->swap_resolver_threads[i]);
        if (sret != ENPU_SUCCESS) {
            LOG_WARN("[ENPU-MGR] Failed to start swap_resolver_thread %d: %d", i, sret);
        }
    }

    LOG_INFO("[ENPU-MGR] Manager started with watchdog thread and %d swap resolver threads",
             mgr->swap_resolver_thread_count);
    return ENPU_SUCCESS;
}

int enpu_manager_stop(enpu_manager_t *mgr)
{
    CHECK_NULL_RET(mgr, ENPU_INVALID_PARAM);

    for (int i = 0; i < mgr->swap_resolver_thread_count; i++) {
        swap_resolver_thread_stop(mgr->swap_resolver_threads[i]);
    }

    atomic_store(&mgr->running, false);

    if (mgr->watchdog != NULL) {
        atomic_store(&mgr->watchdog_running, false);
        pthread_join(mgr->watchdog_thread, NULL);
    }

    LOG_INFO("[ENPU-MGR] Manager stopped");
    return ENPU_SUCCESS;
}

int enpu_manager_allocate(enpu_manager_t *mgr, alloc_request_t *req, alloc_response_t *resp)
{
    CHECK_NULL_RET(mgr, ENPU_INVALID_PARAM);
    CHECK_NULL_RET(req, ENPU_INVALID_PARAM);
    CHECK_NULL_RET(resp, ENPU_INVALID_PARAM);

    if (mgr->allocator == NULL) {
        LOG_ERROR("[ENPU-MGR] Allocator not initialized");
        if (snprintf_s(resp->error_msg, sizeof(resp->error_msg), sizeof(resp->error_msg) - 1,
                       "allocator not initialized") < 0) {
            LOG_ERROR("[ENPU-MGR] snprintf_s failed");
        }
        resp->result = ENPU_FAIL;
        return ENPU_FAIL;
    }

    return npu_allocator_allocate(mgr->allocator, req, resp);
}

int enpu_manager_release(enpu_manager_t *mgr, const char *pod_uid, const char *container_name)
{
    CHECK_NULL_RET(mgr, ENPU_INVALID_PARAM);
    CHECK_NULL_RET(pod_uid, ENPU_INVALID_PARAM);
    CHECK_NULL_RET(container_name, ENPU_INVALID_PARAM);

    if (mgr->allocator == NULL) {
        LOG_ERROR("[ENPU-MGR] Allocator not initialized");
        return ENPU_FAIL;
    }

    return npu_allocator_release(mgr->allocator, pod_uid, container_name);
}

int enpu_manager_release_by_id(enpu_manager_t *mgr, const char *allocation_id)
{
    CHECK_NULL_RET(mgr, ENPU_INVALID_PARAM);
    CHECK_NULL_RET(allocation_id, ENPU_INVALID_PARAM);

    if (mgr->allocator == NULL) {
        LOG_ERROR("[ENPU-MGR] Allocator not initialized");
        return ENPU_FAIL;
    }

    return npu_allocator_release_by_id(mgr->allocator, allocation_id);
}

int enpu_manager_release_all(enpu_manager_t *mgr, int *released_count)
{
    CHECK_NULL_RET(mgr, ENPU_INVALID_PARAM);

    if (mgr->allocator == NULL) {
        LOG_ERROR("[ENPU-MGR] Allocator not initialized");
        return ENPU_FAIL;
    }

    /* 列出当前所有 allocation，逐个 release */
    allocation_t allocs[MAX_ALLOC_COUNT];
    int count = 0;
    allocation_query_t query = {0};
    query.phy_id = -1;
    int ret = npu_allocator_query_allocations(mgr->allocator, &query, allocs, &count);
    if (ret != ENPU_SUCCESS) {
        LOG_ERROR("[ENPU-MGR] release_all: query failed: %d", ret);
        return ret;
    }

    int released = 0;
    for (int i = 0; i < count; i++) {
        int r = npu_allocator_release_by_id(mgr->allocator, allocs[i].allocation_id);
        if (r == ENPU_SUCCESS) {
            released++;
        } else {
            LOG_WARN("[ENPU-MGR] release_all: release allocation_id=%s failed: %d", allocs[i].allocation_id, r);
        }
    }

    if (released_count != NULL) {
        *released_count = released;
    }
    LOG_INFO("[ENPU-MGR] release_all: cleaned %d/%d allocations", released, count);
    return ENPU_SUCCESS;
}

int enpu_manager_query_devices(enpu_manager_t *mgr, const device_query_t *query, device_info_t *devices, int *count)
{
    CHECK_NULL_RET(mgr, ENPU_INVALID_PARAM);
    CHECK_NULL_RET(count, ENPU_INVALID_PARAM);

    if (mgr->allocator == NULL) {
        return ENPU_FAIL;
    }

    int phy_id_filter = -1;
    if (query != NULL) {
        phy_id_filter = query->phy_id;
    }

    npu_meta_t meta[MAX_NPU_PER_NODE];
    npu_allocatable_t allocatable[MAX_NPU_PER_NODE];
    int ret = npu_allocator_query_devices(mgr->allocator, phy_id_filter, meta, allocatable, count);
    if (ret != ENPU_SUCCESS) {
        return ret;
    }

    if (devices != NULL) {
        for (int i = 0; i < *count; i++) {
            if (memcpy_s(&devices[i].meta, sizeof(devices[i].meta), &meta[i], sizeof(npu_meta_t)) != 0) {
                LOG_ERROR("[ENPU-MGR] memcpy_s meta failed");
                return ENPU_FAIL;
            }
            if (memcpy_s(&devices[i].allocatable, sizeof(devices[i].allocatable), &allocatable[i],
                         sizeof(npu_allocatable_t)) != 0) {
                LOG_ERROR("[ENPU-MGR] memcpy_s allocatable failed");
                return ENPU_FAIL;
            }
            devices[i].phy_id = meta[i].phy_id;
            int32_t phy = meta[i].phy_id;
            devices[i].oversub_ratio =
                (phy >= 0 && phy < MAX_NPU_PER_NODE) ? npu_allocator_get_oversub_ratio(mgr->allocator, phy) : 0.0;
        }
    }

    return ENPU_SUCCESS;
}

int enpu_manager_query_allocations(enpu_manager_t *mgr, const allocation_query_t *query, allocation_t *allocations,
                                   int *count)
{
    CHECK_NULL_RET(mgr, ENPU_INVALID_PARAM);
    CHECK_NULL_RET(count, ENPU_INVALID_PARAM);

    if (mgr->allocator == NULL) {
        return ENPU_FAIL;
    }

    return npu_allocator_query_allocations(mgr->allocator, query, allocations, count);
}

int enpu_manager_swap_control(enpu_manager_t *mgr, const char *pod_uid, swap_action_t action)
{
    CHECK_NULL_RET(mgr, ENPU_INVALID_PARAM);
    CHECK_NULL_RET(pod_uid, ENPU_INVALID_PARAM);

    int phy_id = 0, vnpu_id = 0;
    int ret = find_pod_allocation(mgr, pod_uid, &phy_id, &vnpu_id);
    if (ret != ENPU_SUCCESS) {
        LOG_ERROR("[ENPU-MGR] Pod not found for swap control: %s", pod_uid);
        return ENPU_NOT_FOUND;
    }

    switch (action) {
        case SWAP_ACTION_CLEAN:
            return enpu_manager_swap_clean_impl(mgr, phy_id, vnpu_id);
        case SWAP_ACTION_FORCE_OUT:
            return enpu_manager_swap_force_out_impl(mgr, phy_id, vnpu_id, pod_uid);
        case SWAP_ACTION_FORCE_IN:
            return enpu_manager_swap_force_in_impl(mgr, phy_id, vnpu_id, pod_uid);
        default:
            return ENPU_INVALID_PARAM;
    }
}

int enpu_manager_control(enpu_manager_t *mgr, control_op_t op)
{
    CHECK_NULL_RET(mgr, ENPU_INVALID_PARAM);

    if (mgr->allocator == NULL) {
        return ENPU_FAIL;
    }

    switch (op) {
        case CONTROL_OP_CHECKPOINT:
            return npu_allocator_checkpoint(mgr->allocator);
        case CONTROL_OP_RECOVER:
            return npu_allocator_recover(mgr->allocator);
        case CONTROL_OP_HEALTH_CHECK:
            return ENPU_SUCCESS;
        default:
            return ENPU_INVALID_PARAM;
    }
}

int enpu_manager_swap_status(enpu_manager_t *mgr, swap_status_response_t *resp)
{
    CHECK_NULL_RET(mgr, ENPU_INVALID_PARAM);
    CHECK_NULL_RET(resp, ENPU_INVALID_PARAM);

    int ret = memset_s(resp, sizeof(swap_status_response_t), 0, sizeof(swap_status_response_t));
    if (ret != 0) {
        LOG_ERROR("[ENPU-MGR] memset_s resp failed: %d", ret);
        return ENPU_FAIL;
    }

    if (mgr->swap_buf_mgr == NULL) {
        LOG_ERROR("[ENPU-MGR] swap_buf_mgr not initialized");
        return ENPU_FAIL;
    }

    ret = swap_buffer_manager_get_status(mgr->swap_buf_mgr, &resp->buffer_status);
    if (ret != ENPU_SUCCESS) {
        LOG_ERROR("[ENPU-MGR] Failed to get swap buffer status: %d", ret);
        return ret;
    }

    swapped_vnpu_collector_ctx_t ctx = {.resp = resp};
    ret = npu_allocator_foreach_swapped_vnpu(mgr->allocator, collect_swapped_vnpu_for_status, &ctx);
    if (ret != ENPU_SUCCESS) {
        LOG_ERROR("[ENPU-MGR] Failed to foreach swapped vnpu: %d", ret);
        return ret;
    }

    return ENPU_SUCCESS;
}

static int find_pod_allocation(enpu_manager_t *mgr, const char *pod_uid, int *phy_id, int *vnpu_id)
{
    if (mgr->allocator == NULL) {
        return ENPU_NOT_FOUND;
    }

    allocation_registry_t *registry = npu_allocator_get_registry(mgr->allocator);
    if (registry == NULL) {
        return ENPU_NOT_FOUND;
    }

    allocation_t allocations[MAX_ALLOC_COUNT];
    int count = 0;
    int ret = allocation_registry_list(registry, allocations, &count);
    if (ret != ENPU_SUCCESS) {
        return ENPU_NOT_FOUND;
    }

    for (int i = 0; i < count; i++) {
        if (strcmp(allocations[i].pod_uid, pod_uid) == 0) {
            *vnpu_id = allocations[i].vnpu_id;
            *phy_id = allocations[i].phy_id;
            return ENPU_SUCCESS;
        }
    }

    return ENPU_NOT_FOUND;
}

static int enpu_manager_swap_clean_impl(enpu_manager_t *mgr, int phy_id, int vnpu_id)
{
    if (mgr->allocator == NULL || mgr->swap_buf_mgr == NULL) {
        LOG_ERROR("[ENPU-MGR] swap managers not initialized");
        return ENPU_FAIL;
    }

    bool swapped = false;
    uint64_t offset = 0, size = 0;
    int ret = npu_allocator_get_swap_state(mgr->allocator, phy_id, vnpu_id, &swapped, &offset, &size);
    if (ret == ENPU_NOT_FOUND) {
        return ENPU_NOT_FOUND;
    }
    if (ret != ENPU_SUCCESS) {
        LOG_ERROR("[ENPU-MGR] Failed to get swap state: %d", ret);
        return ret;
    }

    if (!swapped) {
        return ENPU_SUCCESS;
    }

    ret = npu_allocator_clear_swap(mgr->allocator, phy_id, vnpu_id);
    if (ret != ENPU_SUCCESS) {
        LOG_ERROR("[ENPU-MGR] Failed to clear swap state: %d", ret);
        return ret;
    }

    return ENPU_SUCCESS;
}

static int enpu_manager_swap_force_out_impl(enpu_manager_t *mgr, int phy_id, int vnpu_id, const char *pod_uid)
{
    if (mgr->swap_resolver == NULL) {
        LOG_ERROR("[ENPU-MGR] swap_resolver not initialized");
        return ENPU_FAIL;
    }

    int ret =
        swap_resolver_write_swap_command(mgr->swap_resolver, phy_id, pod_uid, vnpu_id, SWAP_ACTION_OUT, SWAP_OUT_NONE);
    if (ret != ENPU_SUCCESS) {
        LOG_ERROR("[ENPU-MGR] Failed to write swap out command: %d", ret);
        return ret;
    }

    return ENPU_SUCCESS;
}

static int enpu_manager_swap_force_in_impl(enpu_manager_t *mgr, int phy_id, int vnpu_id, const char *pod_uid)
{
    if (mgr->allocator == NULL) {
        LOG_ERROR("[ENPU-MGR] allocator not initialized");
        return ENPU_FAIL;
    }

    int ret = npu_allocator_write_swap_cmd(mgr->allocator, phy_id, pod_uid, vnpu_id, SWAP_ACTION_IN, SWAP_OUT_NONE);
    if (ret != ENPU_SUCCESS) {
        LOG_ERROR("[ENPU-MGR] Failed to write swap in command: %d", ret);
        return ret;
    }

    return ENPU_SUCCESS;
}
