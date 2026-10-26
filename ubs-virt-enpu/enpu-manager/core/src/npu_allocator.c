/*
 * Copyright (c) Huawei Technologies Co., Ltd. 2025-2026. All rights reserved.
 * enpu-manager is licensed under Mulan PSL v2.
 * You can use this software according to the terms and conditions of the Mulan PSL v2.
 * You may obtain a copy of Mulan PSL v2 at:
 *          http://license.coscl.org.cn/MulanPSL2
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND,
 * EITHER EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT,
 * MERCHANTABILITY OR FIT FOR A PARTICULAR PURPOSE.
 * See the Mulan PSL v2 for more details.
 */

#include "../internal/npu_allocator.h"
#include <pthread.h>
#include <stdarg.h>
#include <stdatomic.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include "../internal/npu_allocator_internal.h"
#include "allocation.h"
#include "common.h"
#include "config_manager.h"
#include "dcmi_adapter.h"
#include "dcmi_stub_adapter.h"
#include "enpu_manager.h"
#include "evaluator.h"
#include "log.h"
#include "npu_tree.h"
#include "rollback_log.h"
#include "securec.h"
#include "shm_manager.h"
#include "storage_adapter.h"

#define MAX_EVALS 3

#define LOG(fmt, ...) LOG_INFO("[ALLOC] " fmt, ##__VA_ARGS__)
#define ERR(fmt, ...) LOG_ERROR("[ALLOC] " fmt, ##__VA_ARGS__)
#define CHECK(p, r)              \
    do {                         \
        if (!(p)) {              \
            ERR("NULL: %s", #p); \
            return (r);          \
        }                        \
    } while (0)

/* 统一安全格式化写入定长缓冲（错误消息/默认值组装）：
 * 内部使用 securec 的 vsnprintf_s 并处理返回值, 调用点无需重复检查 */
static void alloc_snprintf(char *buf, size_t buf_len, const char *fmt, ...)
{
    if (buf == NULL || buf_len == 0 || fmt == NULL) {
        return;
    }
    va_list ap;
    va_start(ap, fmt);
    int ret = vsnprintf_s(buf, buf_len, buf_len - 1, fmt, ap);
    va_end(ap);
    if (ret < 0) {
        ERR("vsnprintf_s failed, ret=%d", ret);
    }
}

/* 统一安全字符串拷贝: 内部使用 securec 的 strncpy_s 并处理返回值 */
static void alloc_copy_str(char *dst, size_t dst_len, const char *src)
{
    if (dst == NULL || dst_len == 0 || src == NULL) {
        return;
    }
    int ret = strncpy_s(dst, dst_len, src, strnlen(src, dst_len - 1));
    if (ret != 0) {
        ERR("strncpy_s failed, ret=%d", ret);
    }
}

/* K8s 路径不调用本函数: mindcluster 写的 npu_info.config 完全信任, 不做此校验 */
int parse_hbm_request_limit(alloc_request_t *req, uint64_t *hbm_request, uint64_t *hbm_limit, char *error_msg)
{
    if (!req || !hbm_request || !hbm_limit) {
        return ENPU_INVALID_PARAM;
    }

    *hbm_request = 0;
    *hbm_limit = 0;
    if (error_msg)
        error_msg[0] = '\0';

    uint64_t user_req = req->hbm_quota;
    uint64_t user_lim = req->hbm_limit;

    switch (req->sched_policy) {
        case SCHED_POLICY_FIXED_SHARE:
            /* fixed-share 不支持显存超分：必须 request > 0 且 request == limit */
            if (user_req == 0) {
                if (error_msg)
                    alloc_snprintf(error_msg, MAX_PATH_LEN, "policy=fixed-share requires hbm_request>0 (got req=%lu)",
                                   (unsigned long)user_req);
                return ENPU_INVALID_PARAM;
            }
            if (user_req != user_lim) {
                if (error_msg)
                    alloc_snprintf(error_msg, MAX_PATH_LEN,
                                   "policy=fixed-share does not support memory oversubscription: "
                                   "requires hbm_request=hbm_limit (got req=%lu, lim=%lu)",
                                   (unsigned long)user_req, (unsigned long)user_lim);
                return ENPU_INVALID_PARAM;
            }
            break;

        case SCHED_POLICY_ELASTIC:
        case SCHED_POLICY_BEST_EFFORT:
            /* 弹性 / 尽力而为：允许任意 0 <= request <= limit */
            if (user_req > user_lim) {
                if (error_msg)
                    alloc_snprintf(error_msg, MAX_PATH_LEN,
                                   "hbm_request must be <= hbm_limit (got req=%lu, lim=%lu, policy=%d)",
                                   (unsigned long)user_req, (unsigned long)user_lim, req->sched_policy);
                return ENPU_INVALID_PARAM;
            }
            break;

        default:
            if (error_msg)
                alloc_snprintf(error_msg, MAX_PATH_LEN,
                               "invalid sched_policy=%d (must be 1=fixed-share, 2=elastic, 3=best-effort)",
                               req->sched_policy);
            return ENPU_INVALID_PARAM;
    }

    *hbm_request = user_req;
    *hbm_limit = user_lim;
    return ENPU_SUCCESS;
}

npu_allocator_t *npu_allocator_create(const allocator_config_t *config)
{
    CHECK(config, NULL);

    npu_tree_t *tree = npu_tree_create();
    if (!tree) {
        ERR("Failed to create tree");
        return NULL;
    }

    npu_allocator_t *a = (npu_allocator_t *)calloc(1, sizeof(npu_allocator_t));
    if (!a) {
        npu_tree_destroy(tree);
        return NULL;
    }

    if (pthread_mutex_init(&a->lock, NULL) != 0) {
        npu_tree_destroy(tree);
        free(a);
        return NULL;
    }

    a->tree = tree;
    a->evaluators = (evaluator_t **)calloc(MAX_EVALS, sizeof(evaluator_t *));
    if (!a->evaluators) {
        pthread_mutex_destroy(&a->lock);
        npu_tree_destroy(tree);
        free(a);
        return NULL;
    }

    a->registry = allocation_registry_create();
    if (!a->registry) {
        free(a->evaluators);
        pthread_mutex_destroy(&a->lock);
        npu_tree_destroy(tree);
        free(a);
        return NULL;
    }

    a->rollback_log = rollback_log_create();
    if (!a->rollback_log) {
        allocation_registry_destroy(a->registry);
        free(a->evaluators);
        pthread_mutex_destroy(&a->lock);
        npu_tree_destroy(tree);
        free(a);
        return NULL;
    }

    a->evaluator_count = 0;
    alloc_copy_str(a->checkpoint_path, MAX_PATH_LEN, config->checkpoint_path);
    alloc_copy_str(a->config_base_path, MAX_PATH_LEN, config->config_base_path);
    alloc_copy_str(a->share_strategy, MAX_NAME_LEN, config->share_strategy);
    alloc_copy_str(a->evaluator_name, MAX_NAME_LEN, config->evaluator_name);
    a->use_dcmi_stub = config->use_dcmi_stub;
    a->dcmi_stub = config->dcmi_stub;

    LOG("Allocator created");
    return a;
}

// conf的share-strategy字符串转为evaluator_share的策略码
// 默认compact
static int share_strategy_from_str(const char *s)
{
    if (s != NULL && strncmp(s, "anti-fragment", 13) == 0) {
        return 1;
    }
    return 0;
}

int npu_allocator_init(npu_allocator_t *a)
{
    CHECK(a, ENPU_INVALID_PARAM);

    pthread_mutex_lock(&a->lock);

    a->eval_registry = evaluator_registry_create();
    if (!a->eval_registry) {
        pthread_mutex_unlock(&a->lock);
        ERR("Failed to create eval_registry");
        return ENPU_FAIL;
    }

    if (!atomic_load(&a->tree->initialized)) {
        int ret = ENPU_SUCCESS;
        if (a->use_dcmi_stub) {
            dcmi_adapter_t *adapter = dcmi_adapter_create_stub(&a->dcmi_stub);
            if (!adapter) {
                evaluator_registry_destroy(a->eval_registry);
                pthread_mutex_unlock(&a->lock);
                ERR("Failed to create stub adapter");
                return ENPU_FAIL;
            }
            ret = npu_tree_init_with_adapter(a->tree, adapter);
            dcmi_adapter_destroy_stub(adapter);
        } else {
            ret = npu_tree_init(a->tree);
        }
        if (ret != ENPU_SUCCESS) {
            evaluator_registry_destroy(a->eval_registry);
            pthread_mutex_unlock(&a->lock);
            ERR("Failed to init tree: %d", ret);
            return ret;
        }
    }

    evaluator_share_init_with_strategy(a->eval_registry, share_strategy_from_str(a->share_strategy));

    evaluator_t *share = evaluator_registry_get(a->eval_registry, "share");

    if (share && a->evaluator_count < MAX_EVALS) {
        a->evaluators[a->evaluator_count++] = share;
    }

    a->config_mgr = config_manager_create(a->config_base_path);
    if (!a->config_mgr) {
        pthread_mutex_unlock(&a->lock);
        ERR("Failed to create config manager");
        return ENPU_FAIL;
    }

    a->shm_mgr = shm_manager_create();
    if (!a->shm_mgr) {
        config_manager_destroy(a->config_mgr);
        a->config_mgr = NULL;
        pthread_mutex_unlock(&a->lock);
        ERR("Failed to create shm manager");
        return ENPU_FAIL;
    }

    storage_adapter_t *storage = file_storage_adapter_create(a->checkpoint_path);
    if (!storage) {
        config_manager_destroy(a->config_mgr);
        shm_manager_destroy(a->shm_mgr);
        a->config_mgr = NULL;
        a->shm_mgr = NULL;
        pthread_mutex_unlock(&a->lock);
        ERR("Failed to create storage adapter");
        return ENPU_FAIL;
    }

    allocation_registry_t *registry_with_adapter = allocation_registry_create_with_adapter(storage);
    if (!registry_with_adapter) {
        storage_adapter_destroy(storage);
        config_manager_destroy(a->config_mgr);
        shm_manager_destroy(a->shm_mgr);
        a->config_mgr = NULL;
        a->shm_mgr = NULL;
        pthread_mutex_unlock(&a->lock);
        ERR("Failed to create registry with adapter");
        return ENPU_FAIL;
    }

    allocation_t existing[MAX_ALLOC_COUNT] = {0};
    int count = 0;
    allocation_registry_list(a->registry, existing, &count);
    for (int i = 0; i < count; i++) {
        allocation_registry_register(registry_with_adapter, &existing[i]);
    }

    allocation_registry_destroy(a->registry);
    a->registry = registry_with_adapter;

    allocation_registry_recover(a->registry);

    // 修复旧 checkpoint 中 buggy 的 shm_id（旧代码写入phy_id, 正确值应为 die_id）
    {
        allocation_t loaded[MAX_ALLOC_COUNT] = {0};
        int loaded_count = 0;
        if (allocation_registry_list(a->registry, loaded, &loaded_count) == ENPU_SUCCESS) {
            int migrated = 0;
            for (int i = 0; i < loaded_count; i++) {
                if (loaded[i].die_id[0] == '\0') {
                    LOG("Skip shm_id migration: pod=%s container=%s has empty die_id", loaded[i].pod_uid,
                        loaded[i].container_name);
                    continue;
                }
                if (strncmp(loaded[i].shm_id, loaded[i].die_id, DIE_ID_LEN) == 0) {
                    continue;
                }

                LOG("Migrating shm_id for pod=%s container=%s: '%s' -> '%s'", loaded[i].pod_uid,
                    loaded[i].container_name, loaded[i].shm_id, loaded[i].die_id);

                // 用真实 die_id 重写 npu_info.config
                vnpu_config_t cfg = {.phy_npu_id = loaded[i].phy_id,
                                     .vnpu_id = loaded[i].vnpu_id,
                                     .aicore_quota = loaded[i].aicore_quota,
                                     .memory_request = loaded[i].hbm_quota,
                                     .memory_limit = loaded[i].hbm_limit,
                                     .scheduling_policy = loaded[i].sched_policy,
                                     .priority = loaded[i].priority};
                alloc_copy_str(cfg.shm_id, SHM_ID_LEN, loaded[i].die_id);
                if (config_manager_write_config(a->config_mgr, loaded[i].pod_uid, loaded[i].container_name, &cfg) !=
                    ENPU_SUCCESS) {
                    ERR("Failed to rewrite npu_info.config during migration: pod=%s container=%s", loaded[i].pod_uid,
                        loaded[i].container_name);
                    continue;
                }

                // 更新 registry 内存 + 持久化 checkpoint
                if (allocation_registry_update_shm_id(a->registry, loaded[i].pod_uid, loaded[i].container_name,
                                                      loaded[i].die_id) != ENPU_SUCCESS) {
                    ERR("Failed to update registry shm_id: pod=%s container=%s", loaded[i].pod_uid,
                        loaded[i].container_name);
                    continue;
                }
                migrated++;
            }
            if (migrated > 0) {
                LOG("Checkpoint migrated: %d allocations, shm_id aligned with die_id", migrated);
            }
        }
    }

    pthread_mutex_unlock(&a->lock);
    LOG("Initialized with %d evaluators", a->evaluator_count);
    int recover_ret = npu_allocator_recover(a);
    if (recover_ret != ENPU_SUCCESS) {
        LOG("Auto recover returned: %d", recover_ret);
    }

    return ENPU_SUCCESS;
}

int npu_allocator_destroy(npu_allocator_t *a)
{
    CHECK(a, ENPU_INVALID_PARAM);

    pthread_mutex_lock(&a->lock);

    if (a->shm_mgr) {
        shm_manager_destroy(a->shm_mgr);
        a->shm_mgr = NULL;
    }

    if (a->config_mgr) {
        config_manager_destroy(a->config_mgr);
        a->config_mgr = NULL;
    }

    if (a->registry) {
        allocation_registry_destroy(a->registry);
        a->registry = NULL;
    }

    if (a->rollback_log) {
        rollback_log_destroy(a->rollback_log);
        a->rollback_log = NULL;
    }

    if (a->evaluators) {
        free(a->evaluators);
        a->evaluators = NULL;
    }

    pthread_mutex_unlock(&a->lock);
    pthread_mutex_destroy(&a->lock);
    free(a);

    LOG("Destroyed");
    return ENPU_SUCCESS;
}

static evaluator_t *pick_evaluator(npu_allocator_t *a)
{
    if (!a || a->evaluator_count == 0)
        return NULL;

    // 优先用配置指定的 evaluator_name；为空则回退到默认 "share"
    // 当前仅注册了 "share", 配置其他名字会查不到而返回 NULL（allocate 即失败）
    const char *target = (a->evaluator_name[0] != '\0') ? a->evaluator_name : "share";

    for (int i = 0; i < a->evaluator_count; i++) {
        if (a->evaluators[i] && strcmp(a->evaluators[i]->name, target) == 0) {
            return a->evaluators[i];
        }
    }
    return NULL;
}

static void build_shm_entry(vnpu_shm_entry_t *entry, int32_t vnpu_id, uint64_t hbm_request, uint64_t hbm_limit,
                            int32_t sched_policy, int32_t swap_priority, bool swap_enabled)
{
    int zero_ret = memset_s(entry, sizeof(*entry), 0, sizeof(*entry));
    if (zero_ret != 0) {
        ERR("memset_s shm entry failed, ret=%d", zero_ret);
    }
    entry->vnpu_id = vnpu_id;
    entry->hbm_request = hbm_request;
    entry->hbm_limit = hbm_limit;
    entry->sched_policy = sched_policy;
    entry->swap_priority = swap_priority;
    entry->swap_enabled = swap_enabled;
}

static void set_alive_time_now(npu_allocator_t *a, int32_t phy_id, int32_t vnpu_id)
{
    vnpu_time_slice_sched_t *sched = shm_manager_get_sched_struct(a->shm_mgr, phy_id);
    if (sched == NULL) {
        return;
    }
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    uint64_t now_ns = (uint64_t)ts.tv_sec * 1000000000ULL + (uint64_t)ts.tv_nsec;
    atomic_store(&sched->last_alive_time_ns[vnpu_id], now_ns);
}

/* 若 (phy_id, vnpu_id) 已存在则先释放 */
static int shm_apply_vnpu_locked(npu_allocator_t *a, int32_t phy_id, const char *die_id, uint64_t hbm_total,
                                 vnpu_shm_entry_t *entry, int32_t priority, char *shm_id_out, int shm_id_out_len)
{
    if (a == NULL || die_id == NULL || entry == NULL) {
        return ENPU_INVALID_PARAM;
    }

    /* 1) 建/获取 shm_state + sched shm */
    char shm_id_buf[SHM_ID_LEN] = {0};
    char *shm_id_target = (shm_id_out != NULL && shm_id_out_len > 0) ? shm_id_out : shm_id_buf;
    int shm_id_target_len = (shm_id_out != NULL && shm_id_out_len > 0) ? shm_id_out_len : SHM_ID_LEN;

    int ret = shm_manager_get_or_create_shm(a->shm_mgr, phy_id, die_id, shm_id_target, shm_id_target_len);
    if (ret != ENPU_SUCCESS) {
        ERR("shm_apply: get_or_create_shm failed: %d", ret);
        return ret;
    }

    /* 2) 初始化 shm_state 的 hbm_total */
    shm_manager_init_state(a->shm_mgr, phy_id, hbm_total);

    /* 3) 若 (phy_id, vnpu_id) 已存在 entry, 先彻底释放
     *    Docker 路径 evaluator 保证 vnpu_id 全新, 此分支不会触发
     *    K8s 路径处理 mindcluster 重写 npu_info.config 的场景 */
    shm_state_t *state = shm_manager_get_state(a->shm_mgr, phy_id);
    if (state != NULL && vnpu_bitmap_test(state->vnpu_bitmap, entry->vnpu_id)) {
        if (state->entries[entry->vnpu_id].swapped) {
            shm_free(state, entry->vnpu_id);
        }
        shm_manager_remove_entry(a->shm_mgr, phy_id, entry->vnpu_id);
        LOG("shm_apply: update existing vnpu phy=%d vnpu=%d", phy_id, entry->vnpu_id);
    }

    /* 4) 加 entry */
    ret = shm_manager_add_entry(a->shm_mgr, phy_id, entry);
    if (ret != ENPU_SUCCESS) {
        ERR("shm_apply: add_entry failed: %d", ret);
        return ret;
    }

    /* 5) 启动宽限期 */
    vnpu_time_slice_sched_t *sched = shm_manager_get_sched_struct(a->shm_mgr, phy_id);
    if (sched != NULL) {
        atomic_store(&sched->vnpu_priority[entry->vnpu_id], (uint8_t)priority);
    }
    set_alive_time_now(a, phy_id, entry->vnpu_id);

    return ENPU_SUCCESS;
}

/* shm release 共用核心：幂等. 
 *   - bitmap 不存在 → 直接 SUCCESS
 *   - swapped → shm_free 释放 swap buffer
 *   - remove_entry（清 bitmap + vnpu_id=-1）
 *   - 清 sched 的 last_alive_time_ns / last_kernel_time_ns（避免 pod_watchdog 误判）
 *
 * 修复点（相对原 Docker release）：
 *   原 Docker npu_allocator_release 只调 shm_manager_remove_entry, 
 *   若 vnpu 处于 swapped 状态会泄漏 swap buffer. 本 helper 统一做完整清理. 
 */
static int shm_release_vnpu_locked(npu_allocator_t *a, int32_t phy_id, int32_t vnpu_id)
{
    if (a == NULL || a->shm_mgr == NULL) {
        return ENPU_INVALID_PARAM;
    }
    if (phy_id < 0 || phy_id >= MAX_NPU_PER_NODE) {
        return ENPU_INVALID_PARAM;
    }
    if (vnpu_id < 0 || vnpu_id >= MAX_VNPU_PER_DIE) {
        return ENPU_INVALID_PARAM;
    }

    shm_state_t *state = shm_manager_get_state(a->shm_mgr, phy_id);
    if (state == NULL) {
        LOG("shm_release: phy=%d shm_state not found, no-op", phy_id);
        return ENPU_SUCCESS;
    }

    /* 幂等：bitmap 不存在直接返回 */
    if (!vnpu_bitmap_test(state->vnpu_bitmap, vnpu_id)) {
        LOG("shm_release: phy=%d vnpu=%d not in bitmap, no-op", phy_id, vnpu_id);
        return ENPU_SUCCESS;
    }

    /* 若 swapped, 先释放 swap buffer */
    if (state->entries[vnpu_id].swapped) {
        shm_free(state, vnpu_id);
    }

    /* 清 entry（bitmap + vnpu_id=-1） */
    int ret = shm_manager_remove_entry(a->shm_mgr, phy_id, vnpu_id);
    if (ret != ENPU_SUCCESS) {
        ERR("shm_release: remove_entry failed: %d", ret);
        return ret;
    }

    /* 清 sched shm 字段, 避免 pod_watchdog 误判已释放的 vnpu */
    vnpu_time_slice_sched_t *sched = shm_manager_get_sched_struct(a->shm_mgr, phy_id);
    if (sched != NULL) {
        atomic_store(&sched->last_alive_time_ns[vnpu_id], 0ULL);
        atomic_store(&sched->last_kernel_time_ns[vnpu_id], 0ULL);
        atomic_store(&sched->vnpu_priority[vnpu_id], VNPU_PRIORITY_NORMAL);
    }

    LOG("shm_release: phy=%d vnpu=%d cleaned", phy_id, vnpu_id);
    return ENPU_SUCCESS;
}

int npu_allocator_allocate(npu_allocator_t *a, alloc_request_t *req, alloc_response_t *resp)
{
    CHECK(a, ENPU_INVALID_PARAM);
    CHECK(req, ENPU_INVALID_PARAM);
    CHECK(resp, ENPU_INVALID_PARAM);

    if (req->priority != VNPU_PRIORITY_NORMAL && req->priority != VNPU_PRIORITY_HIGH) {
        alloc_snprintf(resp->error_msg, MAX_PATH_LEN, "priority must be 0 (normal) or 1 (high), got %d",
                       req->priority);
        resp->result = ENPU_INVALID_PARAM;
        return ENPU_INVALID_PARAM;
    }

    if (req->priority == VNPU_PRIORITY_HIGH) {
        // High priority does not reserve software AICore quota or participate in policy matching.
        req->aicore_quota = 0;
        req->sched_policy = SCHED_POLICY_BEST_EFFORT;
    } else if (req->aicore_quota <= 0) {
        ERR("Invalid aicore_quota: %d", req->aicore_quota);
        alloc_snprintf(resp->error_msg, MAX_PATH_LEN, "aicore_quota must > 0, got %d", req->aicore_quota);
        resp->result = ENPU_INVALID_PARAM;
        return ENPU_INVALID_PARAM;
    }

    if (req->aicore_quota > HUNDRED_CORE) {
        ERR("aicore_quota exceeds single DIE limit: %d > 100", req->aicore_quota);
        alloc_snprintf(resp->error_msg, MAX_PATH_LEN, "aicore_quota exceeds 100 (single DIE limit), got %d",
                       req->aicore_quota);
        resp->result = ENPU_INVALID_PARAM;
        return ENPU_INVALID_PARAM;
    }

    uint64_t hbm_request = 0, hbm_limit = 0;
    int parse_ret = parse_hbm_request_limit(req, &hbm_request, &hbm_limit, resp->error_msg);
    if (parse_ret != ENPU_SUCCESS) {
        resp->result = parse_ret;
        return parse_ret;
    }

    if (req->predicate_phy_id >= 0 && req->predicate_phy_id >= a->tree->leaf_count) {
        ERR("Invalid predicate_phy_id: %d (max: %d)", req->predicate_phy_id, a->tree->leaf_count - 1);
        alloc_snprintf(resp->error_msg, MAX_PATH_LEN, "card %d not found", req->predicate_phy_id);
        resp->result = ENPU_NOT_FOUND;
        return ENPU_NOT_FOUND;
    }

    pthread_mutex_lock(&a->lock);

    if (req->predicate_phy_id >= 0) {
        npu_node_t *target_node = npu_tree_get_leaf(a->tree, req->predicate_phy_id);
        if (target_node == NULL) {
            ERR("Predicate card %d not found", req->predicate_phy_id);
            alloc_snprintf(resp->error_msg, MAX_PATH_LEN, "card %d not found", req->predicate_phy_id);
            resp->result = ENPU_FAIL;
            pthread_mutex_unlock(&a->lock);
            return ENPU_FAIL;
        }
    }

    allocation_t existing = {0};
    if (allocation_registry_get(a->registry, req->pod_uid, req->container_name, &existing) == ENPU_SUCCESS) {
        resp->phy_id = existing.phy_id;
        resp->vnpu_id = existing.vnpu_id;
        alloc_copy_str(resp->pod_uid, MAX_UUID_LEN, existing.pod_uid);
        alloc_copy_str(resp->container_name, MAX_NAME_LEN, existing.container_name);
        alloc_copy_str(resp->die_id, DIE_ID_LEN, existing.die_id);
        resp->aicore_quota = existing.aicore_quota;
        resp->hbm_quota = existing.hbm_quota;
        resp->hbm_limit = existing.hbm_limit;
        resp->sched_policy = existing.sched_policy;
        resp->priority = existing.priority;
        alloc_copy_str(resp->shm_id, SHM_ID_LEN, existing.shm_id);
        resp->result = ENPU_ALREADY_EXISTS;
        alloc_snprintf(resp->error_msg, MAX_PATH_LEN, "vNPU already exists: pod=%s container=%s phy=%d vnpu=%d",
                       existing.pod_uid, existing.container_name, existing.phy_id, existing.vnpu_id);
        pthread_mutex_unlock(&a->lock);
        return ENPU_ALREADY_EXISTS;
    }

    evaluator_t *ev = pick_evaluator(a);
    if (!ev) {
        ERR("No evaluator for aicore_quota %d (VNPU multi-card not supported)", req->aicore_quota);
        alloc_snprintf(resp->error_msg, MAX_PATH_LEN,
                       "no allocator evaluator available for aicore_quota=%d (multi-card vnpu not supported)",
                       req->aicore_quota);
        resp->result = ENPU_INVALID_PARAM;
        pthread_mutex_unlock(&a->lock);
        return ENPU_INVALID_PARAM;
    }

    eval_request_t eval_req = {0};
    eval_req.aicore_quota = (req->aicore_quota > HUNDRED_CORE) ? HUNDRED_CORE : req->aicore_quota;
    eval_req.hbm_quota = hbm_request;
    eval_req.hbm_limit = hbm_limit;
    eval_req.vnpu_count = (req->aicore_quota > HUNDRED_CORE) ? (req->aicore_quota / HUNDRED_CORE) : 1;
    eval_req.sched_policy = req->sched_policy;
    eval_req.priority = req->priority;
    eval_req.predicate_phy_id = req->predicate_phy_id;
    if (memcpy_s(eval_req.oversub_ratio_per_die, sizeof(eval_req.oversub_ratio_per_die), a->oversub_ratio,
                 sizeof(a->oversub_ratio)) != 0) {
        ERR("memcpy_s oversub_ratio failed");
        alloc_snprintf(resp->error_msg, MAX_PATH_LEN, "internal error: copy oversub_ratio failed");
        resp->result = ENPU_FAIL;
        pthread_mutex_unlock(&a->lock);
        return ENPU_FAIL;
    }

    for (int phy_id = 0; phy_id < MAX_NPU_PER_NODE; phy_id++) {
        shm_state_t *state = shm_manager_get_state(a->shm_mgr, phy_id);
        if (state == NULL) {
            continue;
        }
        uint64_t sum = 0;
        for (int v = 0; v < MAX_VNPU_PER_DIE; v++) {
            if (vnpu_bitmap_test(state->vnpu_bitmap, v)) {
                sum += state->entries[v].hbm_limit;
            }
        }
        eval_req.allocated_hbm_limit_per_die[phy_id] = sum;
    }

    eval_response_t eval_resp = {0};
    eval_resp.nodes = (npu_node_t **)calloc(eval_req.vnpu_count, sizeof(npu_node_t *));
    if (!eval_resp.nodes) {
        alloc_snprintf(resp->error_msg, MAX_PATH_LEN, "OOM: failed to alloc evaluator nodes buffer");
        resp->result = ENPU_FAIL;
        pthread_mutex_unlock(&a->lock);
        return ENPU_FAIL;
    }

    int ret = ev->evaluate(ev, a->tree, &eval_req, &eval_resp);
    if (ret != ENPU_SUCCESS) {
        ERR("Evaluator %s failed: %d", ev->name, ret);
        if (eval_resp.error_msg[0] != '\0') {
            alloc_snprintf(resp->error_msg, MAX_PATH_LEN, "allocation failed: %s", eval_resp.error_msg);
        } else if (req->predicate_phy_id >= 0 && ret == ENPU_NO_RESOURCE) {
            alloc_snprintf(resp->error_msg, MAX_PATH_LEN, "card %d insufficient resources", req->predicate_phy_id);
        } else {
            alloc_snprintf(resp->error_msg, MAX_PATH_LEN, "allocation failed: %s", ev->name);
        }
        resp->result = ret;
        free(eval_resp.nodes);
        pthread_mutex_unlock(&a->lock);
        return ret;
    }

    npu_node_t *node = eval_resp.nodes[0];
    if (!node) {
        alloc_snprintf(resp->error_msg, MAX_PATH_LEN, "no node selected");
        resp->result = ENPU_FAIL;
        free(eval_resp.nodes);
        pthread_mutex_unlock(&a->lock);
        return ENPU_FAIL;
    }

    int32_t phy_id = npu_node_get_phy_id(node);
    int32_t vnpu_id = eval_resp.vnpu_ids[0];

    /* pod_uid / container_name 是可选字段, 用户未指定时填默认值 */
    if (req->pod_uid[0] == '\0') {
        alloc_snprintf(req->pod_uid, MAX_UUID_LEN, "pod-%d", phy_id);
        LOG("pod_uid not specified, using default: %s", req->pod_uid);
    }
    if (req->container_name[0] == '\0') {
        alloc_snprintf(req->container_name, MAX_NAME_LEN, "container-%d-%d", phy_id, vnpu_id);
        LOG("container_name not specified, using default: %s", req->container_name);
    }

    allocation_t alloc = {0};
    alloc.vnpu_id = vnpu_id;
    alloc_copy_str(alloc.pod_uid, MAX_UUID_LEN, req->pod_uid);
    alloc_copy_str(alloc.container_name, MAX_NAME_LEN, req->container_name);
    alloc.aicore_quota = eval_req.aicore_quota;
    alloc.hbm_quota = hbm_request;
    alloc.hbm_limit = hbm_limit;
    alloc.sched_policy = req->sched_policy;
    alloc.priority = req->priority;
    alloc.swap_priority = (req->swap_priority >= SWAP_PRIORITY_HIGH && req->swap_priority <= SWAP_PRIORITY_LOW) ?
                              req->swap_priority :
                              SWAP_PRIORITY_MEDIUM;
    alloc_copy_str(alloc.die_id, DIE_ID_LEN, npu_node_get_die_id(node));
    alloc.create_time_ns = (uint64_t)time(NULL) * 1000000000ULL;

    a->rollback_log->step_count = 0;

    ret = npu_tree_mark_vnpu_occupied(a->tree, node, alloc.vnpu_id);
    if (ret != ENPU_SUCCESS) {
        alloc_snprintf(resp->error_msg, MAX_PATH_LEN, "mark_vnpu_occupied failed (phy=%d vnpu=%d): ret=%d", phy_id,
                       alloc.vnpu_id, ret);
        resp->result = ret;
        free(eval_resp.nodes);
        pthread_mutex_unlock(&a->lock);
        return ret;
    }
    rollback_log_append(a->rollback_log, STEP_MARK_OCCUPIED, phy_id, alloc.vnpu_id, 0, 0, 0, "", "", "");

    ret = npu_tree_update_quota(a->tree, node, -alloc.aicore_quota, -(int64_t)alloc.hbm_quota);
    if (ret != ENPU_SUCCESS) {
        rollback_log_rollback(a->rollback_log, a);
        alloc_snprintf(resp->error_msg, MAX_PATH_LEN, "update_quota failed (phy=%d vnpu=%d, aicore=%d hbm=%lu): ret=%d",
                       phy_id, alloc.vnpu_id, alloc.aicore_quota, (unsigned long)alloc.hbm_quota, ret);
        resp->result = ret;
        free(eval_resp.nodes);
        pthread_mutex_unlock(&a->lock);
        return ret;
    }
    rollback_log_append(a->rollback_log, STEP_UPDATE_QUOTA, phy_id, alloc.vnpu_id, -alloc.aicore_quota,
                        -(int64_t)alloc.hbm_quota, 0, "", "", "");

    ret = npu_tree_attach_policy(a->tree, node, alloc.vnpu_id, alloc.sched_policy, alloc.priority);
    if (ret != ENPU_SUCCESS) {
        rollback_log_rollback(a->rollback_log, a);
        alloc_snprintf(resp->error_msg, MAX_PATH_LEN, "attach_policy failed (phy=%d vnpu=%d sched_policy=%d): ret=%d",
                       phy_id, alloc.vnpu_id, alloc.sched_policy, ret);
        resp->result = ret;
        free(eval_resp.nodes);
        pthread_mutex_unlock(&a->lock);
        return ret;
    }
    rollback_log_append(a->rollback_log, STEP_ATTACH_POLICY, phy_id, alloc.vnpu_id, 0, 0, alloc.sched_policy, "", "",
                        "");

    int resp_zero_ret = memset_s(resp, sizeof(alloc_response_t), 0, sizeof(alloc_response_t));
    if (resp_zero_ret != 0) {
        ERR("memset_s resp failed, ret=%d", resp_zero_ret);
    }

    vnpu_shm_entry_t shm_entry;
    build_shm_entry(&shm_entry, alloc.vnpu_id, hbm_request, hbm_limit, alloc.sched_policy, alloc.swap_priority,
                    a->oversub_ratio[phy_id] > 0);

    ret = shm_apply_vnpu_locked(a, phy_id, alloc.die_id, npu_node_get_total_memory(node), &shm_entry, alloc.priority,
                                resp->shm_id, SHM_ID_LEN);
    if (ret != ENPU_SUCCESS) {
        rollback_log_rollback(a->rollback_log, a);
        alloc_snprintf(resp->error_msg, MAX_PATH_LEN, "shm_apply_vnpu_locked failed (phy=%d die=%s vnpu=%d): ret=%d",
                       phy_id, alloc.die_id, alloc.vnpu_id, ret);
        resp->result = ret;
        free(eval_resp.nodes);
        pthread_mutex_unlock(&a->lock);
        ERR("Failed to create shm: %d", ret);
        return ret;
    }
    rollback_log_append(a->rollback_log, STEP_CREATE_SHM, phy_id, alloc.vnpu_id, 0, 0, 0, "", "", resp->shm_id);

    allocation_t registry_alloc = {0};
    registry_alloc.phy_id = npu_node_get_phy_id(node);
    registry_alloc.vnpu_id = alloc.vnpu_id;
    alloc_copy_str(registry_alloc.die_id, DIE_ID_LEN, npu_node_get_die_id(node));
    registry_alloc.aicore_quota = alloc.aicore_quota;
    registry_alloc.hbm_quota = hbm_request;
    registry_alloc.hbm_limit = hbm_limit;
    registry_alloc.sched_policy = alloc.sched_policy;
    registry_alloc.priority = alloc.priority;
    registry_alloc.swap_priority = alloc.swap_priority;
    registry_alloc.create_time_ns = alloc.create_time_ns;
    alloc_copy_str(registry_alloc.pod_uid, MAX_UUID_LEN, req->pod_uid);
    alloc_copy_str(registry_alloc.container_name, MAX_NAME_LEN, req->container_name);
    alloc_copy_str(registry_alloc.shm_id, SHM_ID_LEN, resp->shm_id);

    vnpu_config_t config = {.phy_npu_id = registry_alloc.phy_id,
                            .vnpu_id = registry_alloc.vnpu_id,
                            .aicore_quota = registry_alloc.aicore_quota,
                            .memory_request = registry_alloc.hbm_quota,
                            .memory_limit = registry_alloc.hbm_limit,
                            .scheduling_policy = registry_alloc.sched_policy};
    config.priority = registry_alloc.priority;
    alloc_copy_str(config.shm_id, SHM_ID_LEN, registry_alloc.shm_id);

    ret = config_manager_write_config(a->config_mgr, req->pod_uid, req->container_name, &config);
    if (ret != ENPU_SUCCESS) {
        rollback_log_rollback(a->rollback_log, a);
        alloc_snprintf(resp->error_msg, MAX_PATH_LEN, "write npu_info.config failed (pod=%s container=%s): ret=%d",
                       req->pod_uid, req->container_name, ret);
        resp->result = ret;
        free(eval_resp.nodes);
        pthread_mutex_unlock(&a->lock);
        ERR("Failed to write config: %d", ret);
        return ret;
    }
    rollback_log_append(a->rollback_log, STEP_WRITE_CONFIG, phy_id, alloc.vnpu_id, 0, 0, 0, req->pod_uid,
                        req->container_name, "");

    resp->phy_id = registry_alloc.phy_id;
    resp->vnpu_id = registry_alloc.vnpu_id;
    alloc_copy_str(resp->pod_uid, MAX_UUID_LEN, registry_alloc.pod_uid);
    alloc_copy_str(resp->container_name, MAX_NAME_LEN, registry_alloc.container_name);
    alloc_copy_str(resp->die_id, DIE_ID_LEN, registry_alloc.die_id);
    resp->aicore_quota = registry_alloc.aicore_quota;
    resp->hbm_quota = registry_alloc.hbm_quota;
    resp->hbm_limit = registry_alloc.hbm_limit;
    resp->sched_policy = registry_alloc.sched_policy;
    resp->priority = registry_alloc.priority;
    alloc_copy_str(resp->shm_id, SHM_ID_LEN, registry_alloc.shm_id);
    alloc_copy_str(resp->minor_name, MAX_NAME_LEN, npu_node_get_uuid(node));
    resp->result = ENPU_SUCCESS;

    ret = allocation_registry_register(a->registry, &registry_alloc);
    if (ret != ENPU_SUCCESS) {
        rollback_log_rollback(a->rollback_log, a);
        alloc_snprintf(resp->error_msg, MAX_PATH_LEN,
                       "allocation_registry_register failed (pod=%s container=%s phy=%d vnpu=%d): ret=%d", req->pod_uid,
                       req->container_name, phy_id, alloc.vnpu_id, ret);
        resp->result = ret;
        free(eval_resp.nodes);
        pthread_mutex_unlock(&a->lock);
        ERR("Failed to register in registry: %d", ret);
        return ret;
    }
    rollback_log_append(a->rollback_log, STEP_SAVE_CHECKPOINT, phy_id, alloc.vnpu_id, 0, 0, 0, req->pod_uid,
                        req->container_name, "");

    free(eval_resp.nodes);
    pthread_mutex_unlock(&a->lock);

    resp->result = ENPU_SUCCESS;
    LOG("Allocated vNPU %d on phy %d for pod %s", resp->vnpu_id, resp->phy_id, req->pod_uid);
    return ENPU_SUCCESS;
}

int npu_allocator_release(npu_allocator_t *a, const char *pod_uid, const char *container_name)
{
    CHECK(a, ENPU_INVALID_PARAM);
    CHECK(pod_uid, ENPU_INVALID_PARAM);
    CHECK(container_name, ENPU_INVALID_PARAM);

    pthread_mutex_lock(&a->lock);

    allocation_t alloc = {0};
    if (allocation_registry_get(a->registry, pod_uid, container_name, &alloc) != ENPU_SUCCESS) {
        pthread_mutex_unlock(&a->lock);
        ERR("Allocation not found: %s/%s", pod_uid, container_name);
        return ENPU_NOT_FOUND;
    }

    npu_node_t *node = npu_tree_get_leaf(a->tree, alloc.phy_id);
    if (node) {
        uint64_t vnpu_mask = npu_node_get_vnpu_mask(node);
        uint64_t bit_mask = (1ULL << alloc.vnpu_id);
        if ((vnpu_mask & bit_mask) != 0) {
            LOG("Release: vNPU %d not occupied in npu_tree , skip quota update", alloc.vnpu_id);
        } else {
            npu_tree_detach_policy(a->tree, node, alloc.vnpu_id);
            npu_tree_update_quota(a->tree, node, alloc.aicore_quota, (int64_t)alloc.hbm_quota);
            npu_tree_mark_vnpu_free(a->tree, node, alloc.vnpu_id);
        }
        shm_release_vnpu_locked(a, alloc.phy_id, alloc.vnpu_id);
    }

    config_manager_delete_config(a->config_mgr, pod_uid, container_name);

    int ret = allocation_registry_release(a->registry, pod_uid, container_name);
    if (ret != ENPU_SUCCESS) {
        LOG("Registry release failed but continuing: %d", ret);
    }

    pthread_mutex_unlock(&a->lock);
    LOG("Released vNPU for pod %s container %s", pod_uid, container_name);
    return ENPU_SUCCESS;
}

int npu_allocator_release_by_vnpu(npu_allocator_t *a, int phy_id, int vnpu_id)
{
    CHECK(a, ENPU_INVALID_PARAM);
    if (vnpu_id < 0 || vnpu_id >= MAX_VNPU_PER_DIE) {
        return ENPU_INVALID_PARAM;
    }

    // 按 (phy_id, vnpu_id) 反查 registry 拿到 (pod_uid, container), 再走正常 release
    pthread_mutex_lock(&a->lock);
    allocation_t allocs[MAX_ALLOC_COUNT];
    int count = 0;
    int ret = allocation_registry_list(a->registry, allocs, &count);
    char pod_uid[MAX_UUID_LEN] = {0};
    char container_name[MAX_NAME_LEN] = {0};
    bool found = false;
    if (ret == ENPU_SUCCESS) {
        for (int i = 0; i < count; i++) {
            if (allocs[i].phy_id == phy_id && allocs[i].vnpu_id == vnpu_id) {
                alloc_copy_str(pod_uid, MAX_UUID_LEN, allocs[i].pod_uid);
                alloc_copy_str(container_name, MAX_NAME_LEN, allocs[i].container_name);
                found = true;
                break;
            }
        }
    }
    pthread_mutex_unlock(&a->lock);

    if (!found) {
        return ENPU_NOT_FOUND;
    }
    return npu_allocator_release(a, pod_uid, container_name);
}

/* ==========================================================================
 * K8s 场景：外部组件（mindcluster）写 npu_info.config 后, 
 * enpu-manager 只需根据解析出的信息建立/更新 shm. 
 * ========================================================================== */

int npu_allocator_release_external_vnpu(npu_allocator_t *a, int32_t phy_id, int32_t vnpu_id)
{
    CHECK(a, ENPU_INVALID_PARAM);
    if (phy_id < 0 || phy_id >= MAX_NPU_PER_NODE) {
        return ENPU_INVALID_PARAM;
    }
    if (vnpu_id < 0 || vnpu_id >= MAX_VNPU_PER_DIE) {
        return ENPU_INVALID_PARAM;
    }

    pthread_mutex_lock(&a->lock);
    int ret = shm_release_vnpu_locked(a, phy_id, vnpu_id);
    pthread_mutex_unlock(&a->lock);
    return ret;
}

int npu_allocator_recover(npu_allocator_t *a)
{
    CHECK(a, ENPU_INVALID_PARAM);

    pthread_mutex_lock(&a->lock);

    allocation_t allocs[MAX_ALLOC_COUNT] = {0};
    int count = 0;
    int ret = allocation_registry_list(a->registry, allocs, &count);
    if (ret != ENPU_SUCCESS || count == 0) {
        pthread_mutex_unlock(&a->lock);
        LOG("No allocations to recover");
        return ENPU_SUCCESS;
    }

    int recovered_count = 0;
    for (int i = 0; i < count; i++) {
        allocation_t *alloc = &allocs[i];

        npu_node_t *node = npu_tree_get_leaf(a->tree, alloc->phy_id);
        if (!node) {
            LOG("Skip recovery: node %d not found", alloc->phy_id);
            continue;
        }

        npu_tree_mark_vnpu_occupied(a->tree, node, alloc->vnpu_id);
        npu_tree_update_quota(a->tree, node, -alloc->aicore_quota, -(int64_t)alloc->hbm_quota);
        npu_tree_attach_policy(a->tree, node, alloc->vnpu_id, alloc->sched_policy, alloc->priority);

        /* 恢复 shm_state（共享内存） */
        char shm_id_out[SHM_ID_LEN] = {0};
        ret = shm_manager_get_or_create_shm(a->shm_mgr, alloc->phy_id, alloc->die_id, shm_id_out, SHM_ID_LEN);
        if (ret != ENPU_SUCCESS) {
            LOG("Recover: get_or_create_shm failed for phy=%d, skip", alloc->phy_id);
            continue;
        }

        shm_state_t *state = shm_manager_get_state(a->shm_mgr, alloc->phy_id);
        vnpu_time_slice_sched_t *sched = shm_manager_get_sched_struct(a->shm_mgr, alloc->phy_id);
        if (sched != NULL) {
            atomic_store(&sched->vnpu_priority[alloc->vnpu_id], (uint8_t)alloc->priority);
        }
        if (state != NULL) {
            shm_manager_init_state(a->shm_mgr, alloc->phy_id, npu_node_get_total_memory(node));
            vnpu_bitmap_set(state->vnpu_bitmap, alloc->vnpu_id);
            state->entries[alloc->vnpu_id].vnpu_id = alloc->vnpu_id;
            state->entries[alloc->vnpu_id].hbm_request = alloc->hbm_quota;
            state->entries[alloc->vnpu_id].hbm_limit = alloc->hbm_limit;
            state->entries[alloc->vnpu_id].sched_policy = alloc->sched_policy;
            state->entries[alloc->vnpu_id].swap_priority = alloc->swap_priority;
            state->entries[alloc->vnpu_id].swap_enabled = true;
            atomic_store(&state->entries[alloc->vnpu_id].hbm_used, 0);
        }

        /* 重写 npu_info.config */
        vnpu_config_t config = {
            .phy_npu_id = alloc->phy_id,
            .vnpu_id = alloc->vnpu_id,
            .aicore_quota = alloc->aicore_quota,
            .memory_request = alloc->hbm_quota,
            .memory_limit = alloc->hbm_limit,
            .scheduling_policy = alloc->sched_policy,
            .priority = alloc->priority,
        };
        alloc_copy_str(config.shm_id, SHM_ID_LEN, alloc->shm_id);
        if (config_manager_write_config(a->config_mgr, alloc->pod_uid, alloc->container_name, &config) !=
            ENPU_SUCCESS) {
            ERR("Recover: write npu_info.config failed for pod=%s container=%s, skip", alloc->pod_uid,
                alloc->container_name);
            continue;
        }

        recovered_count++;
    }

    pthread_mutex_unlock(&a->lock);
    LOG("Recovered %d allocations", recovered_count);
    return ENPU_SUCCESS;
}

int npu_allocator_checkpoint(npu_allocator_t *a)
{
    CHECK(a, ENPU_INVALID_PARAM);

    pthread_mutex_lock(&a->lock);
    int ret = allocation_registry_save(a->registry);
    if (ret != ENPU_SUCCESS) {
        ERR("Checkpoint save to %s failed: %d", a->checkpoint_path, ret);
    } else {
        LOG("Checkpoint saved to %s", a->checkpoint_path);
    }
    pthread_mutex_unlock(&a->lock);

    return ret;
}

int npu_allocator_query_allocations(npu_allocator_t *a, const allocation_query_t *query, allocation_t *result,
                                    int *count)
{
    CHECK(a, ENPU_INVALID_PARAM);
    CHECK(query, ENPU_INVALID_PARAM);
    CHECK(result, ENPU_INVALID_PARAM);
    CHECK(count, ENPU_INVALID_PARAM);

    pthread_mutex_lock(&a->lock);

    if (query->phy_id >= 0 && query->phy_id >= a->tree->leaf_count) {
        pthread_mutex_unlock(&a->lock);
        ERR("Invalid phy_id: %d", query->phy_id);
        return ENPU_INVALID_PARAM;
    }

    allocation_t all_allocs[MAX_ALLOC_COUNT];
    int all_count = 0;
    int ret = allocation_registry_list(a->registry, all_allocs, &all_count);
    if (ret != ENPU_SUCCESS) {
        pthread_mutex_unlock(&a->lock);
        return ret;
    }

    int filtered_count = 0;
    for (int i = 0; i < all_count && filtered_count < MAX_ALLOC_COUNT; i++) {
        bool match = true;
        if (query->phy_id >= 0 && all_allocs[i].phy_id != query->phy_id) {
            match = false;
        }
        if (strlen(query->pod_uid) > 0 && strcmp(all_allocs[i].pod_uid, query->pod_uid) != 0) {
            match = false;
        }
        if (strlen(query->container_name) > 0 && strcmp(all_allocs[i].container_name, query->container_name) != 0) {
            match = false;
        }
        if (match) {
            result[filtered_count++] = all_allocs[i];
        }
    }

    *count = filtered_count;
    pthread_mutex_unlock(&a->lock);
    LOG("Query returned %d allocations", filtered_count);
    return ENPU_SUCCESS;
}

int npu_allocator_query_devices(npu_allocator_t *a, int32_t phy_id, npu_meta_t *meta, npu_allocatable_t *allocatable,
                                int *count)
{
    CHECK(a, ENPU_INVALID_PARAM);
    CHECK(meta, ENPU_INVALID_PARAM);
    CHECK(allocatable, ENPU_INVALID_PARAM);
    CHECK(count, ENPU_INVALID_PARAM);

    pthread_mutex_lock(&a->lock);

    if (phy_id >= 0 && phy_id >= a->tree->leaf_count) {
        *count = 0;
        pthread_mutex_unlock(&a->lock);
        return ENPU_SUCCESS;
    }

    int found = 0;
    for (int i = 0; i < a->tree->leaf_count; i++) {
        if (phy_id >= 0 && i != phy_id) {
            continue;
        }

        npu_node_t *node = a->tree->leaves[i];
        if (!node) {
            continue;
        }

        meta[found].id = npu_node_get_id(node);
        meta[found].phy_id = npu_node_get_phy_id(node);
        alloc_copy_str(meta[found].die_id, DIE_ID_LEN, npu_node_get_die_id(node));
        meta[found].minor_id = npu_node_get_minor_id(node);
        alloc_copy_str(meta[found].uuid, MAX_UUID_LEN, npu_node_get_uuid(node));
        alloc_copy_str(meta[found].minor_name, MAX_NAME_LEN, npu_node_get_minor_name(node));
        meta[found].total_memory = npu_node_get_total_memory(node);
        alloc_copy_str(meta[found].card_type, MAX_NAME_LEN, npu_node_get_card_type(node));

        allocatable[found].aicore_quota = npu_node_get_aicore_quota(node);
        allocatable[found].hbm_quota = npu_node_get_hbm_quota(node);
        allocatable[found].vnpu_count = npu_node_get_vnpu_count(node);
        allocatable[found].vnpu_mask = npu_node_get_vnpu_mask(node);

        found++;
    }

    *count = found;
    pthread_mutex_unlock(&a->lock);
    LOG("Query returned %d devices", found);
    return ENPU_SUCCESS;
}

int npu_allocator_get_swap_state(npu_allocator_t *a, int32_t phy_id, int vnpu_id, bool *swapped, uint64_t *offset,
                                 uint64_t *size)
{
    CHECK(a, ENPU_INVALID_PARAM);
    CHECK(swapped, ENPU_INVALID_PARAM);
    CHECK(offset, ENPU_INVALID_PARAM);
    CHECK(size, ENPU_INVALID_PARAM);

    pthread_mutex_lock(&a->lock);

    int ret = shm_manager_get_swap_state(a->shm_mgr, phy_id, vnpu_id, swapped, offset, size);

    pthread_mutex_unlock(&a->lock);
    return ret;
}

int npu_allocator_set_swap_state(npu_allocator_t *a, int32_t phy_id, int vnpu_id, uint64_t offset, uint64_t size)
{
    CHECK(a, ENPU_INVALID_PARAM);

    pthread_mutex_lock(&a->lock);

    int ret = shm_manager_set_swap_state(a->shm_mgr, phy_id, vnpu_id, offset, size);

    pthread_mutex_unlock(&a->lock);
    return ret;
}

int npu_allocator_clear_swap(npu_allocator_t *a, int32_t phy_id, int vnpu_id)
{
    CHECK(a, ENPU_INVALID_PARAM);

    pthread_mutex_lock(&a->lock);

    shm_state_t *state = shm_manager_get_state(a->shm_mgr, phy_id);
    if (state != NULL) {
        shm_free(state, vnpu_id);
    }
    int ret = shm_manager_set_swap_state(a->shm_mgr, phy_id, vnpu_id, 0, 0);

    pthread_mutex_unlock(&a->lock);
    return ret;
}

int npu_allocator_write_swap_cmd(npu_allocator_t *a, int32_t phy_id, const char *pod_uid, int vnpu_id, int action,
                                 uint8_t to_swap_out)
{
    CHECK(a, ENPU_INVALID_PARAM);
    CHECK(pod_uid, ENPU_INVALID_PARAM);

    pthread_mutex_lock(&a->lock);

    int ret = shm_manager_write_swap_cmd(a->shm_mgr, phy_id, pod_uid, vnpu_id, action, to_swap_out);
    if (ret != ENPU_SUCCESS) {
        /* 命令未写入成功时不做任何清理: swap 动作未发生, buffer 状态保持原样, 由调用方决定重试 */
        ERR("shm_manager_write_swap_cmd failed (phy=%d vnpu=%d action=%d): ret=%d", phy_id, vnpu_id, action, ret);
    }

    pthread_mutex_unlock(&a->lock);
    return ret;
}

int npu_allocator_get_last_kernel_time_ns(npu_allocator_t *a, int phy_id, int vnpu_id, uint64_t *last_kernel_time_ns)
{
    CHECK(a, ENPU_INVALID_PARAM);
    CHECK(last_kernel_time_ns, ENPU_INVALID_PARAM);
    if (vnpu_id < 0 || vnpu_id >= MAX_VNPU_PER_DIE) {
        return ENPU_INVALID_PARAM;
    }

    pthread_mutex_lock(&a->lock);

    if (a->shm_mgr == NULL) {
        pthread_mutex_unlock(&a->lock);
        return ENPU_NOT_FOUND;
    }

    vnpu_time_slice_sched_t *sched = shm_manager_get_sched_struct(a->shm_mgr, phy_id);
    if (sched == NULL) {
        pthread_mutex_unlock(&a->lock);
        return ENPU_NOT_FOUND;
    }

    *last_kernel_time_ns = atomic_load(&sched->last_kernel_time_ns[vnpu_id]);

    pthread_mutex_unlock(&a->lock);
    return ENPU_SUCCESS;
}

allocation_registry_t *npu_allocator_get_registry(npu_allocator_t *alloc)
{
    if (alloc == NULL)
        return NULL;
    return alloc->registry;
}

int npu_allocator_foreach_swapped_vnpu(npu_allocator_t *a, swapped_vnpu_callback_t callback, void *user_data)
{
    CHECK(a, ENPU_INVALID_PARAM);
    CHECK(callback, ENPU_INVALID_PARAM);

    pthread_mutex_lock(&a->lock);

    if (a->shm_mgr == NULL) {
        pthread_mutex_unlock(&a->lock);
        return ENPU_SUCCESS;
    }

    for (int phy_id = 0; phy_id < MAX_NPU_PER_NODE; phy_id++) {
        shm_state_t *state = a->shm_mgr->shm_states[phy_id];
        if (state == NULL)
            continue;

        uint64_t bitmap0 = atomic_load(&state->vnpu_bitmap[0]);
        uint64_t bitmap1 = atomic_load(&state->vnpu_bitmap[1]);

        for (int vnpu_id = 0; vnpu_id < MAX_VNPU_PER_DIE; vnpu_id++) {
            int idx = vnpu_id / 64;
            int bit = vnpu_id % 64;
            uint64_t bitmap = (idx == 0) ? bitmap0 : bitmap1;

            if ((bitmap >> bit) & 1) {
                vnpu_shm_entry_t *entry = &state->entries[vnpu_id];
                if (entry->swapped) {
                    callback(phy_id, entry->vnpu_id, entry->swap_size, user_data);
                }
            }
        }
    }

    pthread_mutex_unlock(&a->lock);
    return ENPU_SUCCESS;
}

shm_state_t *npu_allocator_get_shm_state(npu_allocator_t *a, int phy_id)
{
    if (a == NULL)
        return NULL;

    pthread_mutex_lock(&a->lock);
    shm_state_t *state = shm_manager_get_state(a->shm_mgr, phy_id);
    pthread_mutex_unlock(&a->lock);
    return state;
}

shm_manager_t *npu_allocator_get_shm_manager(npu_allocator_t *a)
{
    CHECK(a, NULL);
    return a->shm_mgr;
}

uint64_t npu_allocator_get_total_hbm_mb(npu_allocator_t *a)
{
    if (a == NULL || a->tree == NULL) {
        return 0;
    }
    uint64_t sum_mb = 0;
    pthread_mutex_lock(&a->tree->tree_lock);
    for (int i = 0; i < a->tree->leaf_count; i++) {
        npu_node_t *node = a->tree->leaves[i];
        if (node != NULL) {
            sum_mb += npu_node_get_total_memory(node) / (1024ULL * 1024ULL);
        }
    }
    pthread_mutex_unlock(&a->tree->tree_lock);
    return sum_mb;
}

/* 按 phy_id 索引返回每张卡的 HBM 总量（MB） */
int npu_allocator_get_per_die_hbm_mb(npu_allocator_t *a, uint64_t *out_hbm_mb, int max_count)
{
    if (a == NULL || a->tree == NULL || out_hbm_mb == NULL || max_count <= 0) {
        return ENPU_INVALID_PARAM;
    }
    int fill_count = (max_count < MAX_NPU_PER_NODE) ? max_count : MAX_NPU_PER_NODE;
    int zero_ret = memset_s(out_hbm_mb, sizeof(uint64_t) * fill_count, 0, sizeof(uint64_t) * fill_count);
    if (zero_ret != 0) {
        ERR("memset_s out_hbm_mb failed, ret=%d", zero_ret);
        return ENPU_FAIL;
    }

    pthread_mutex_lock(&a->tree->tree_lock);
    for (int i = 0; i < a->tree->leaf_count; i++) {
        npu_node_t *node = a->tree->leaves[i];
        if (node == NULL) {
            continue;
        }
        int32_t phy_id = npu_node_get_phy_id(node);
        if (phy_id >= 0 && phy_id < fill_count) {
            out_hbm_mb[phy_id] = npu_node_get_total_memory(node) / (1024ULL * 1024ULL);
        }
    }
    pthread_mutex_unlock(&a->tree->tree_lock);
    return ENPU_SUCCESS;
}

int npu_allocator_set_oversub_ratio(npu_allocator_t *a, int die_index, double ratio)
{
    CHECK(a, ENPU_INVALID_PARAM);
    if (die_index < 0 || die_index >= MAX_NPU_PER_NODE) {
        return ENPU_INVALID_PARAM;
    }

    pthread_mutex_lock(&a->lock);
    a->oversub_ratio[die_index] = ratio;
    pthread_mutex_unlock(&a->lock);
    return ENPU_SUCCESS;
}

double npu_allocator_get_oversub_ratio(npu_allocator_t *a, int die_index)
{
    if (a == NULL || die_index < 0 || die_index >= MAX_NPU_PER_NODE) {
        return 0.0;
    }

    pthread_mutex_lock(&a->lock);
    double ratio = a->oversub_ratio[die_index];
    pthread_mutex_unlock(&a->lock);
    return ratio;
}
