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

#ifndef __NPU_TREE_H__
#define __NPU_TREE_H__

#include <pthread.h>
#include <stdatomic.h>
#include "allocation.h"
#include "common.h"

struct dcmi_adapter;
typedef struct dcmi_adapter dcmi_adapter_t;

typedef struct npu_node npu_node_t;

typedef struct npu_tree {
    pthread_mutex_t tree_lock;
    npu_node_t *root;
    npu_node_t **leaves;
    int leaf_count;
    npu_node_t **die_nodes;
    int die_node_count;
    npu_node_t **query_table;
    int query_size;
    bool real_mode;
    atomic_bool initialized;
} npu_tree_t;

#if defined(__cplusplus)
extern "C" {
#endif

npu_tree_t *npu_tree_create(void);
int npu_tree_init(npu_tree_t *tree);
int npu_tree_init_with_adapter(npu_tree_t *tree, dcmi_adapter_t *adapter);
int npu_tree_destroy(npu_tree_t *tree);

npu_node_t *npu_tree_query(npu_tree_t *tree, const char *minor_name);
npu_node_t *npu_tree_get_leaf(npu_tree_t *tree, int phy_id);

int32_t npu_node_get_available_aicore(npu_node_t *node);
uint64_t npu_node_get_available_hbm(npu_node_t *node);
int npu_node_get_available_vnpu_id(npu_node_t *node);
int npu_node_get_vnpu_count(npu_node_t *node);

int32_t npu_node_get_phy_id(npu_node_t *node);
int32_t npu_node_get_id(npu_node_t *node);
const char *npu_node_get_die_id(npu_node_t *node);
int32_t npu_node_get_minor_id(npu_node_t *node);
const char *npu_node_get_minor_name(npu_node_t *node);
npu_topo_level_t npu_node_get_topo_level(npu_node_t *node);
npu_node_t *npu_node_get_parent(npu_node_t *node);
const char *npu_node_get_uuid(npu_node_t *node);
uint64_t npu_node_get_total_memory(npu_node_t *node);
const char *npu_node_get_card_type(npu_node_t *node);

int32_t npu_node_get_aicore_quota(npu_node_t *node);
uint64_t npu_node_get_hbm_quota(npu_node_t *node);
uint64_t npu_node_get_vnpu_mask(npu_node_t *node);

int npu_tree_get_available_vnpu_id(npu_node_t *node);
uint64_t npu_tree_get_available_aicore(npu_node_t *node);
uint64_t npu_tree_get_available_hbm(npu_node_t *node);

int npu_tree_mark_occupied(npu_tree_t *tree, npu_node_t *node, int vnpu_id, allocation_t *alloc);
int npu_tree_mark_free(npu_tree_t *tree, npu_node_t *node, int vnpu_id);
int npu_tree_mark_vnpu_occupied(npu_tree_t *tree, npu_node_t *node, int vnpu_id);
int npu_tree_mark_vnpu_free(npu_tree_t *tree, npu_node_t *node, int vnpu_id);
int npu_tree_update_quota(npu_tree_t *tree, npu_node_t *node, int32_t aicore_delta, int64_t hbm_delta);
int npu_tree_attach_policy(npu_tree_t *tree, npu_node_t *node, int vnpu_id, int32_t sched_policy, int32_t priority);
int npu_tree_detach_policy(npu_tree_t *tree, npu_node_t *node, int vnpu_id);
int npu_node_validate_policy(npu_node_t *node, int32_t sched_policy);

int npu_tree_save_checkpoint(npu_tree_t *tree, const char *path);
int npu_tree_load_checkpoint(npu_tree_t *tree, const char *path);

#if defined(__cplusplus)
}
#endif

#endif
