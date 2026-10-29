/*
 * Copyright (c) Huawei Technologies Co., Ltd. 2025-2026. All rights reserved.
 * enpu-manager is licensed under Mulan PSL v2.
 * You can use this software according to the terms and conditions of the Mulan PSL v2.
 * You may obtain a copy of Mulan PSL v2 at:
 *          http://license.coscl.org.cn/MulanPSL2
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND,
 * EITHER EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT,
 * MERCHANTABILITY OR FIT FOR A SPECIFIC PURPOSE.
 * See the Mulan PSL v2 for more details.
 */

#ifndef __EVALUATOR_H__
#define __EVALUATOR_H__

#include "common.h"

struct npu_tree;
struct npu_node;
typedef struct npu_tree npu_tree_t;
typedef struct npu_node npu_node_t;

typedef struct evaluator evaluator_t;
typedef struct evaluator_registry evaluator_registry_t;

typedef struct eval_request {
    int32_t aicore_quota;
    uint64_t hbm_quota;
    uint64_t hbm_limit;
    int32_t vnpu_count;
    int32_t sched_policy;
    int32_t vnpu_priority;
    int32_t predicate_phy_id;
    double oversub_ratio_per_die[MAX_NPU_PER_NODE];
    uint64_t allocated_hbm_limit_per_die[MAX_NPU_PER_NODE]; /* 每 DIE 已分配的 hbm_limit 总和（MB）, 索引=phy_id */
} eval_request_t;

typedef struct eval_response {
    npu_node_t **nodes;
    int node_count;
    int32_t vnpu_ids[MAX_VNPU_PER_DIE];
    char error_msg[MAX_PATH_LEN];
} eval_response_t;

typedef int (*evaluate_func)(evaluator_t *eval, npu_tree_t *tree, eval_request_t *req, eval_response_t *resp);

struct evaluator {
    const char *name;
    evaluate_func evaluate;
    void *private_data;
};

#if defined(__cplusplus)
extern "C" {
#endif

evaluator_registry_t *evaluator_registry_create(void);
void evaluator_registry_destroy(evaluator_registry_t *registry);
int evaluator_registry_register(evaluator_registry_t *registry, evaluator_t *eval);
evaluator_t *evaluator_registry_get(evaluator_registry_t *registry, const char *name);
void evaluator_registry_clear(evaluator_registry_t *registry);

int evaluator_share_init_with_registry(evaluator_registry_t *registry);
int evaluator_share_init_with_strategy(evaluator_registry_t *registry, int strategy);
void evaluator_share_set_strategy(int strategy);

#if defined(__cplusplus)
}
#endif

#endif
