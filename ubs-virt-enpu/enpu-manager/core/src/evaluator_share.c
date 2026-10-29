/*
 * Copyright (c) Huawei Technologies Co., Ltd. 2025-2026. All rights reserved.
 * enpu-manager is licensed under Mulan PSL v2.
 */

#include <stdatomic.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "common.h"
#include "evaluator.h"
#include "log.h"
#include "npu_tree.h"
#include "securec.h"

#define CHECK_NULL_RET(param, ret) \
    do {                           \
        if ((param) == NULL) {     \
            return (ret);          \
        }                          \
    } while (0)

#define MAX_CANDIDATES MAX_NPU_PER_NODE

typedef struct share_candidate {
    npu_node_t *node;
    int32_t vnpu_id;
    int32_t avail_aicore;
    uint64_t avail_hbm;
    int32_t vnpu_count;
    int32_t minor_id;
    int32_t vnpu_priority;
    bool policy_ok;
    bool die_oversub_enabled; /* 该 die 是否启用显存超分（oversub_ratio > 0） */
} share_candidate_t;

typedef enum
{
    SHARE_STRATEGY_COMPACT = 0,
    SHARE_STRATEGY_ANTI_FRAGMENT = 1,
} share_strategy_t;

static share_strategy_t g_share_strategy = SHARE_STRATEGY_COMPACT;

static int compare_compact(const void *a, const void *b)
{
    const share_candidate_t *ca = (const share_candidate_t *)a;
    const share_candidate_t *cb = (const share_candidate_t *)b;

    if (ca->vnpu_count != cb->vnpu_count) {
        return cb->vnpu_count - ca->vnpu_count;
    }

    if (ca->avail_aicore != cb->avail_aicore) {
        return ca->avail_aicore - cb->avail_aicore;
    }

    return ca->minor_id - cb->minor_id;
}

static int compare_anti_fragment(const void *a, const void *b)
{
    const share_candidate_t *ca = (const share_candidate_t *)a;
    const share_candidate_t *cb = (const share_candidate_t *)b;

    if (ca->vnpu_count != cb->vnpu_count) {
        return ca->vnpu_count - cb->vnpu_count;
    }

    if (ca->avail_aicore != cb->avail_aicore) {
        return cb->avail_aicore - ca->avail_aicore;
    }

    return ca->minor_id - cb->minor_id;
}

static int compare_load_balance(const void *a, const void *b)
{
    const share_candidate_t *ca = (const share_candidate_t *)a;
    const share_candidate_t *cb = (const share_candidate_t *)b;

    if (ca->vnpu_count != cb->vnpu_count) {
        return ca->vnpu_count - cb->vnpu_count;
    }
    if (ca->avail_hbm != cb->avail_hbm) {
        return (ca->avail_hbm < cb->avail_hbm) ? 1 : -1;
    }
    return ca->minor_id - cb->minor_id;
}

static int compare_share_candidates(const void *a, const void *b)
{
    const share_candidate_t *ca = (const share_candidate_t *)a;
    const share_candidate_t *cb = (const share_candidate_t *)b;

    if (ca->vnpu_priority == VNPU_PRIORITY_HIGH) {
        return compare_load_balance(a, b);
    }

    /* 非超分 die 优先：vNPU 不支持显存超分时, 候选里既有 oversub die 又有非 oversub die, 
     * 把非 oversub die 排前面. vNPU 支持超分时所有候选都是 oversub die, 此规则不生效.  */
    if (ca->die_oversub_enabled != cb->die_oversub_enabled) {
        return ca->die_oversub_enabled ? 1 : -1;
    }

    if (g_share_strategy == SHARE_STRATEGY_COMPACT) {
        return compare_compact(a, b);
    }
    return compare_anti_fragment(a, b);
}

static int evaluator_share_evaluate(evaluator_t *eval, npu_tree_t *tree, eval_request_t *req, eval_response_t *resp)
{
    CHECK_NULL_RET(eval, ENPU_INVALID_PARAM);
    CHECK_NULL_RET(tree, ENPU_INVALID_PARAM);
    CHECK_NULL_RET(req, ENPU_INVALID_PARAM);
    CHECK_NULL_RET(resp, ENPU_INVALID_PARAM);

    // g_share_strategy 是策略的唯一来源, 由 evaluator_share_set_strategy /
    // evaluator_share_init_with_strategy 在初始化阶段写入（值来自 conf 的 share-strategy）,
    // compare_share_candidates 直接读它, 无需在此从 private_data 中转拷贝.

    if ((req->vnpu_priority == VNPU_PRIORITY_NORMAL && req->aicore_quota <= 0) || req->hbm_quota == 0) {
        return ENPU_INVALID_PARAM;
    }

    if (req->vnpu_priority != VNPU_PRIORITY_NORMAL && req->vnpu_priority != VNPU_PRIORITY_HIGH) {
        return ENPU_INVALID_PARAM;
    }

    if (req->vnpu_priority == VNPU_PRIORITY_NORMAL &&
        (req->sched_policy < SCHED_POLICY_FIXED_SHARE || req->sched_policy > SCHED_POLICY_BEST_EFFORT)) {
        return ENPU_INVALID_PARAM;
    }

    if (!atomic_load(&tree->initialized)) {
        return ENPU_FAIL;
    }

    const char *strategy_name = (g_share_strategy == SHARE_STRATEGY_COMPACT) ? "compact" : "anti-fragment";
    LOG_INFO("evaluator_share: strategy=%s, request aicore_quota=%d hbm_quota=%lu sched_policy=%d predicate_phy_id=%d, "
             "leaf_count=%d",
             strategy_name, req->aicore_quota, (unsigned long)req->hbm_quota, req->sched_policy, req->predicate_phy_id,
             tree->leaf_count);

    share_candidate_t candidates[MAX_CANDIDATES];
    int candidate_count = 0;
    int ret;

    /* 记录最近一次分配失败 */
    const char *last_reject_reason = NULL;
    int last_reject_phy_id = -1;
    uint64_t last_reject_die_total = 0;
    uint64_t last_reject_allocated = 0;
    uint64_t last_reject_threshold = 0;

    pthread_mutex_lock(&tree->tree_lock);

    for (int i = 0; i < tree->leaf_count && candidate_count < MAX_CANDIDATES; i++) {
        npu_node_t *node = tree->leaves[i];

        if (req->predicate_phy_id >= 0 && npu_node_get_phy_id(node) != req->predicate_phy_id) {
            continue;
        }

        int32_t avail_aicore = npu_node_get_available_aicore(node);
        uint64_t avail_hbm = npu_node_get_available_hbm(node);
        int32_t phy_id = npu_node_get_phy_id(node);

        /* phy_id 来自硬件设备号, 作为 per-die 数组下标前必须校验范围, 防止越界访问 */
        if (phy_id < 0 || phy_id >= MAX_NPU_PER_NODE) {
            last_reject_phy_id = phy_id;
            last_reject_reason = "phy_id out of range for per-die arrays";
            LOG_INFO("phy_id=%d REJECTED: out of range [0, %d)", phy_id, MAX_NPU_PER_NODE);
            continue;
        }

        uint64_t die_hbm_total_mb = npu_node_get_total_memory(node) / (1024ULL * 1024ULL);

        /* 显存超分约束：vNPU 需要超分（hbm_request<hbm_limit）只能进 oversub_ratio>0 的 die;
         * vNPU 不需要超分时两类 die 都进候选, 由排序阶段优先非超分 die */
        bool die_oversub_enabled = (req->oversub_ratio_per_die[phy_id] > 0.0);
        bool vnpu_oversub = (req->hbm_quota < req->hbm_limit);
        if (vnpu_oversub && !die_oversub_enabled) {
            last_reject_phy_id = phy_id;
            last_reject_reason = "vNPU requires oversubscription (hbm_request<hbm_limit) but die has oversub_ratio=0";
            LOG_INFO("phy_id=%d REJECTED: vNPU needs oversub (req=%lu < lim=%lu) but die oversub_ratio=0", phy_id,
                     (unsigned long)req->hbm_quota, (unsigned long)req->hbm_limit);
            continue;
        }

        /* hbm_limit不超过DIE的hbm_total */
        if (req->hbm_limit > die_hbm_total_mb) {
            last_reject_phy_id = phy_id;
            last_reject_reason = "request hbm_limit exceeds die hbm_total";
            last_reject_die_total = die_hbm_total_mb;
            LOG_INFO("phy_id=%d REJECTED: req hbm_limit=%lu MB > die_total=%lu MB", phy_id,
                     (unsigned long)req->hbm_limit, (unsigned long)die_hbm_total_mb);
            continue;
        }

        /* DIE上所有pod的hbm_limit总和（已分配 + 本次申请）不超过 (1 + oversub_ratio_per_die[phy_id]) * die_hbm_total */
        uint64_t allocated = req->allocated_hbm_limit_per_die[phy_id];
        uint64_t new_total = allocated + req->hbm_limit;
        double die_oversub = req->oversub_ratio_per_die[phy_id];
        uint64_t threshold = (uint64_t)((1.0 + die_oversub) * (double)die_hbm_total_mb);
        if (new_total > threshold) {
            last_reject_phy_id = phy_id;
            last_reject_reason = "hbm_limit sum exceeds (1+oversub_ratio_per_die)*die_hbm_total";
            last_reject_die_total = die_hbm_total_mb;
            last_reject_allocated = allocated;
            last_reject_threshold = threshold;
            LOG_INFO("phy_id=%d REJECTED: allocated=%lu + req=%lu = %lu MB > threshold=%lu MB (oversub_ratio=%.3f)",
                     phy_id, (unsigned long)allocated, (unsigned long)req->hbm_limit, (unsigned long)new_total,
                     (unsigned long)threshold, die_oversub);
            continue;
        }

        if (avail_aicore < req->aicore_quota || avail_hbm < req->hbm_quota) {
            last_reject_phy_id = phy_id;
            last_reject_reason = "insufficient aicore or hbm quota";
            LOG_INFO("phy_id=%d REJECTED: avail_aicore=%d (req %d), avail_hbm=%lu MB (req %lu MB)", phy_id,
                     avail_aicore, req->aicore_quota, (unsigned long)avail_hbm, (unsigned long)req->hbm_quota);
            continue;
        }

        int vnpu_id = npu_tree_get_available_vnpu_id(node);
        if (vnpu_id < 0) {
            last_reject_phy_id = phy_id;
            last_reject_reason = "no free vnpu slot on this die";
            LOG_INFO("phy_id=%d REJECTED: no free vnpu slot", phy_id);
            continue;
        }

        candidates[candidate_count].node = node;
        candidates[candidate_count].vnpu_id = vnpu_id;
        candidates[candidate_count].avail_aicore = avail_aicore;
        candidates[candidate_count].avail_hbm = avail_hbm;
        candidates[candidate_count].vnpu_count = npu_node_get_vnpu_count(node);
        candidates[candidate_count].minor_id = npu_node_get_minor_id(node);
        candidates[candidate_count].vnpu_priority = req->vnpu_priority;
        candidates[candidate_count].policy_ok = (req->vnpu_priority == VNPU_PRIORITY_HIGH) ||
                                                (npu_node_validate_policy(node, req->sched_policy) == ENPU_SUCCESS);
        candidates[candidate_count].die_oversub_enabled = die_oversub_enabled;
        LOG_INFO("candidate[%d]: phy_id=%d vnpu_count=%d avail_aicore=%d avail_hbm=%lu -> vnpu_id=%d policy_ok=%d",
                 candidate_count, phy_id, candidates[candidate_count].vnpu_count, avail_aicore,
                 (unsigned long)avail_hbm, vnpu_id, candidates[candidate_count].policy_ok);
        candidate_count++;
    }

    pthread_mutex_unlock(&tree->tree_lock);

    if (candidate_count == 0) {
        /* 全失败了, 就透传error消息 */
        if (last_reject_reason != NULL) {
            if (last_reject_allocated > 0 || last_reject_threshold > 0) {
                ret = snprintf_s(resp->error_msg, MAX_PATH_LEN, MAX_PATH_LEN - 1,
                                 "all DIEs rejected; last reason: phy_id=%d %s (allocated=%lu MB, threshold=%lu MB, "
                                 "die_total=%lu MB)",
                                 last_reject_phy_id, last_reject_reason, (unsigned long)last_reject_allocated,
                                 (unsigned long)last_reject_threshold, (unsigned long)last_reject_die_total);
            } else {
                ret = snprintf_s(resp->error_msg, MAX_PATH_LEN, MAX_PATH_LEN - 1,
                                 "all DIEs rejected; last reason: phy_id=%d %s (die_total=%lu MB)", last_reject_phy_id,
                                 last_reject_reason, (unsigned long)last_reject_die_total);
            }
        } else {
            ret = snprintf_s(resp->error_msg, MAX_PATH_LEN, MAX_PATH_LEN - 1, "no DIE candidate available");
        }
        if (ret < 0) {
            LOG_ERROR("evaluator_share: snprintf_s error_msg failed, ret=%d", ret);
        }
        return ENPU_NO_RESOURCE;
    }

    /* 指定了卡id */
    if (req->predicate_phy_id >= 0) {
        if (!candidates[0].policy_ok) {
            ret = snprintf_s(resp->error_msg, MAX_PATH_LEN, MAX_PATH_LEN - 1,
                             "card %d rejected: sched_policy=%d conflicts with existing vNPUs on the DIE",
                             req->predicate_phy_id, req->sched_policy);
            if (ret < 0) {
                LOG_ERROR("evaluator_share: snprintf_s error_msg failed, ret=%d", ret);
            }
            return ENPU_NO_RESOURCE;
        }
        resp->nodes[0] = candidates[0].node;
        resp->node_count = 1;
        resp->vnpu_ids[0] = candidates[0].vnpu_id;
        LOG_INFO("evaluator_share: PREDICATE mode (direct allocate) phy_id=%d vnpu_id=%d",
                 npu_node_get_phy_id(candidates[0].node), candidates[0].vnpu_id);
        return ENPU_SUCCESS;
    }

    /* 未指定卡id */
    qsort(candidates, candidate_count, sizeof(share_candidate_t), compare_share_candidates);

    for (int i = 0; i < candidate_count; i++) {
        if (!candidates[i].policy_ok) {
            continue;
        }

        resp->nodes[0] = candidates[i].node;
        resp->node_count = 1;
        resp->vnpu_ids[0] = candidates[i].vnpu_id;

        LOG_INFO("evaluator_share: SELECT phy_id=%d vnpu_id=%d (vnpu_count=%d avail_aicore=%d)",
                 npu_node_get_phy_id(candidates[i].node), candidates[i].vnpu_id, candidates[i].vnpu_count,
                 candidates[i].avail_aicore);
        return ENPU_SUCCESS;
    }

    /* 候选存在但全部sched_policy不匹配 */
    ret = snprintf_s(resp->error_msg, MAX_PATH_LEN, MAX_PATH_LEN - 1,
                     "%d candidate DIEs passed resource/oversub checks but all rejected sched_policy=%d (must match "
                     "existing vNPUs on the DIE)",
                     candidate_count, req->sched_policy);
    if (ret < 0) {
        LOG_ERROR("evaluator_share: snprintf_s error_msg failed, ret=%d", ret);
    }
    return ENPU_NO_RESOURCE;
}

// share_evaluator 是单例, 策略由文件级唯一全局 g_share_strategy 承载,
// private_data 不再使用（保持 NULL）. compare_share_candidates 直接读 g_share_strategy.
static evaluator_t share_evaluator = {
    .name = "share",
    .evaluate = evaluator_share_evaluate,
    .private_data = NULL,
};

void evaluator_share_set_strategy(int strategy)
{
    if (strategy == 0) {
        g_share_strategy = SHARE_STRATEGY_COMPACT;
    } else {
        g_share_strategy = SHARE_STRATEGY_ANTI_FRAGMENT;
    }
}

int evaluator_share_init_with_registry(evaluator_registry_t *registry)
{
    if (registry == NULL) {
        return ENPU_INVALID_PARAM;
    }
    // 测试入口：用当前 g_share_strategy（默认 compact, 测试可用 evaluator_share_set_strategy 覆盖）
    return evaluator_registry_register(registry, &share_evaluator);
}

int evaluator_share_init_with_strategy(evaluator_registry_t *registry, int strategy)
{
    if (registry == NULL) {
        return ENPU_INVALID_PARAM;
    }
    evaluator_share_set_strategy(strategy);
    return evaluator_registry_register(registry, &share_evaluator);
}
