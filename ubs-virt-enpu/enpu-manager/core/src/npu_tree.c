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

#include "npu_tree.h"
#include <pthread.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include "common.h"
#include "dcmi_adapter.h"
#include "dcmi_wrapper.h"
#include "log.h"
#include "securec.h"

typedef struct vnpu_policy_entry {
    int32_t vnpu_id;
    int32_t sched_policy;
    int32_t priority;
} vnpu_policy_entry_t;

struct npu_node {
    npu_meta_t meta;
    npu_allocatable_t allocatable;
    vnpu_policy_entry_t *policy_entries;
    int policy_entry_count;
    struct npu_node *parent;
    struct npu_node **children;
    int children_count;
    uint64_t mask; /* 64 位掩码, 覆盖 MAX_NPU_PER_NODE 内全部设备号（1ULL << phy_id） */
    npu_topo_level_t topo_level;
    pthread_mutex_t lock;
};

#define CHECK_NULL_RET(param, ret)                                     \
    do {                                                               \
        if ((param) == NULL) {                                         \
            LOG_ERROR("[NPU-TREE] Invalid param: %s is NULL", #param); \
            return (ret);                                              \
        }                                                              \
    } while (0)

static npu_node_t *create_node(void)
{
    npu_node_t *node = (npu_node_t *)calloc(1, sizeof(npu_node_t));
    if (node == NULL) {
        LOG_ERROR("[NPU-TREE] Failed to allocate node memory");
        return NULL;
    }

    pthread_mutex_init(&node->lock, NULL);
    node->children = (npu_node_t **)calloc(MAX_CHILDREN, sizeof(npu_node_t *));
    if (node->children == NULL) {
        LOG_ERROR("[NPU-TREE] Failed to allocate children array");
        free(node);
        return NULL;
    }
    node->children_count = 0;
    node->parent = NULL;
    node->mask = 0;
    node->topo_level = NPU_TOPO_CARD;
    node->policy_entries = NULL;
    node->policy_entry_count = 0;

    return node;
}

static void destroy_node(npu_node_t *node)
{
    if (node == NULL) {
        return;
    }

    pthread_mutex_lock(&node->lock);

    for (int i = 0; i < node->children_count; i++) {
        destroy_node(node->children[i]);
    }
    free(node->children);

    if (node->policy_entries != NULL) {
        free(node->policy_entries);
    }

    pthread_mutex_unlock(&node->lock);
    pthread_mutex_destroy(&node->lock);
    free(node);
}

static int build_tree_hierarchy(npu_tree_t *tree)
{
    tree->root = create_node();
    if (tree->root == NULL) {
        LOG_ERROR("[NPU-TREE] Failed to create root node");
        return ENPU_FAIL;
    }

    tree->root->meta.id = 0;
    tree->root->topo_level = NPU_TOPO_SYS;
    tree->root->mask = 0;

    return ENPU_SUCCESS;
}

npu_tree_t *npu_tree_create(void)
{
    npu_tree_t *tree = (npu_tree_t *)calloc(1, sizeof(npu_tree_t));
    if (tree == NULL) {
        LOG_ERROR("[NPU-TREE] Failed to allocate tree memory");
        return NULL;
    }

    pthread_mutex_init(&tree->tree_lock, NULL);
    tree->root = NULL;
    tree->leaves = (npu_node_t **)calloc(MAX_NPU_PER_NODE, sizeof(npu_node_t *));
    if (tree->leaves == NULL) {
        LOG_ERROR("[NPU-TREE] Failed to allocate leaves array");
        free(tree);
        return NULL;
    }
    tree->leaf_count = 0;

    tree->die_nodes = (npu_node_t **)calloc(MAX_NPU_PER_NODE, sizeof(npu_node_t *));
    if (tree->die_nodes == NULL) {
        LOG_ERROR("[NPU-TREE] Failed to allocate die_nodes array");
        free(tree->leaves);
        free(tree);
        return NULL;
    }
    tree->die_node_count = 0;

    tree->query_table = (npu_node_t **)calloc(MAX_NPU_PER_NODE, sizeof(npu_node_t *));
    if (tree->query_table == NULL) {
        LOG_ERROR("[NPU-TREE] Failed to allocate query_table");
        free(tree->die_nodes);
        free(tree->leaves);
        free(tree);
        return NULL;
    }
    tree->query_size = 0;
    tree->real_mode = true;
    atomic_store(&tree->initialized, false);

    return tree;
}

int npu_tree_init(npu_tree_t *tree)
{
    CHECK_NULL_RET(tree, ENPU_INVALID_PARAM);

    pthread_mutex_lock(&tree->tree_lock);

    if (atomic_load(&tree->initialized)) {
        pthread_mutex_unlock(&tree->tree_lock);
        LOG_DEBUG("[NPU-TREE] Tree already initialized");
        return ENPU_SUCCESS;
    }

    int ret = dcmi_wrapper_init();
    if (ret != ENPU_SUCCESS) {
        pthread_mutex_unlock(&tree->tree_lock);
        LOG_ERROR("[NPU-TREE] Failed to init DCMI wrapper");
        return ENPU_FAIL;
    }

    int device_count = 0;
    ret = dcmi_get_device_count(&device_count);
    if (ret != ENPU_SUCCESS || device_count <= 0) {
        pthread_mutex_unlock(&tree->tree_lock);
        LOG_ERROR("[NPU-TREE] Failed to get device count or no devices");
        return ENPU_FAIL;
    }

    if (device_count > MAX_NPU_PER_NODE) {
        device_count = MAX_NPU_PER_NODE;
    }

    int *logic_ids = (int *)calloc(device_count, sizeof(int));
    if (logic_ids == NULL) {
        pthread_mutex_unlock(&tree->tree_lock);
        return ENPU_FAIL;
    }

    int actual_count = 0;
    ret = dcmi_get_device_list(logic_ids, device_count, &actual_count);
    if (ret != ENPU_SUCCESS) {
        free(logic_ids);
        pthread_mutex_unlock(&tree->tree_lock);
        LOG_ERROR("[NPU-TREE] Failed to get device list");
        return ENPU_FAIL;
    }

    for (int i = 0; i < actual_count; i++) {
        dcmi_device_info_t dev_info = {0};

        ret = dcmi_get_device_info(logic_ids[i], &dev_info);
        if (ret != ENPU_SUCCESS) {
            LOG_DEBUG("[NPU-TREE] Failed to get device info for logic_id %d", logic_ids[i]);
            continue;
        }

        npu_node_t *node = create_node();
        if (node == NULL) {
            free(logic_ids);
            pthread_mutex_unlock(&tree->tree_lock);
            return ENPU_FAIL;
        }

        node->meta.id = logic_ids[i];
        node->meta.phy_id = logic_ids[i]; /* phy_id == logic_id（全局物理编号, 对应 npu-smi 设备号）*/
        node->meta.minor_id = logic_ids[i];
        ret = strncpy_s(node->meta.uuid, MAX_UUID_LEN, dev_info.uuid, strnlen(dev_info.uuid, MAX_UUID_LEN - 1));
        if (ret != 0) {
            LOG_ERROR("[NPU-TREE] strncpy_s uuid failed, ret=%d", ret);
            free(node);
            free(logic_ids);
            pthread_mutex_unlock(&tree->tree_lock);
            return ENPU_FAIL;
        }
        ret = snprintf_s(node->meta.minor_name, MAX_NAME_LEN, MAX_NAME_LEN - 1, "Ascend%d", logic_ids[i]);
        if (ret < 0) {
            LOG_ERROR("[NPU-TREE] snprintf_s minor_name failed, ret=%d", ret);
            free(node);
            free(logic_ids);
            pthread_mutex_unlock(&tree->tree_lock);
            return ENPU_FAIL;
        }
        node->meta.total_memory = (uint64_t)dev_info.total_memory * 1024ULL * 1024ULL;
        ret = strncpy_s(node->meta.card_type, MAX_NAME_LEN, "Ascend910B", strlen("Ascend910B"));
        if (ret != 0) {
            LOG_ERROR("[NPU-TREE] strncpy_s card_type failed, ret=%d", ret);
            free(node);
            free(logic_ids);
            pthread_mutex_unlock(&tree->tree_lock);
            return ENPU_FAIL;
        }

        node->topo_level = NPU_TOPO_DIE;
        node->mask = (1ULL << logic_ids[i]);

        atomic_store(&node->allocatable.aicore_quota, (int)dev_info.total_aicore);
        atomic_store(&node->allocatable.hbm_quota, dev_info.total_memory);
        atomic_store(&node->allocatable.vnpu_count, 0);
        node->allocatable.vnpu_mask = 0xFFFFFFFFFFFFFFFFULL;

        dcmi_die_info_t die_info = {0};
        int die_ret = dcmi_get_die_info(logic_ids[i], &die_info);
        if (die_ret != ENPU_SUCCESS) {
            LOG_ERROR("[NPU-TREE] Failed to get die_info for logic_id=%d (ret=%d)", logic_ids[i], die_ret);
            free(node);
            free(logic_ids);
            pthread_mutex_unlock(&tree->tree_lock);
            return ENPU_FAIL;
        }
        ret = strncpy_s(node->meta.die_id, DIE_ID_LEN, die_info.die_id, strnlen(die_info.die_id, DIE_ID_LEN - 1));
        if (ret != 0) {
            LOG_ERROR("[NPU-TREE] strncpy_s die_id failed, ret=%d", ret);
            free(node);
            free(logic_ids);
            pthread_mutex_unlock(&tree->tree_lock);
            return ENPU_FAIL;
        }

        tree->leaves[tree->leaf_count++] = node;
        LOG_INFO("[NPU-TREE] npu_tree: built DIE phy_id=%d die_id=%s hbm=%luMB aicore=%d", node->meta.phy_id,
                 node->meta.die_id, (unsigned long)dev_info.total_memory, dev_info.total_aicore);
        if (logic_ids[i] < MAX_NPU_PER_NODE) {
            tree->query_table[logic_ids[i]] = node;
        }

        /* 预留能力: DIE→CARD 层级关系（card_nodes 数组 / parent 链）由后续交付版本补充, 
         * 当前 DIE 节点 parent 保持 NULL, mask 汇聚逻辑暂不生效.  */
    }

    build_tree_hierarchy(tree);

    tree->query_size = tree->leaf_count;
    atomic_store(&tree->initialized, true);

    free(logic_ids);
    pthread_mutex_unlock(&tree->tree_lock);

    LOG_INFO("[NPU-TREE] npu_tree: initialized with %d DIE leaves", tree->leaf_count);
    return ENPU_SUCCESS;
}

int npu_tree_init_with_adapter(npu_tree_t *tree, dcmi_adapter_t *adapter)
{
    CHECK_NULL_RET(tree, ENPU_INVALID_PARAM);
    CHECK_NULL_RET(adapter, ENPU_INVALID_PARAM);

    pthread_mutex_lock(&tree->tree_lock);

    if (atomic_load(&tree->initialized)) {
        pthread_mutex_unlock(&tree->tree_lock);
        LOG_DEBUG("[NPU-TREE] Tree already initialized");
        return ENPU_SUCCESS;
    }

    int device_count = 0;
    int ret = adapter->get_device_count(adapter, &device_count);
    if (ret != ENPU_SUCCESS || device_count <= 0) {
        pthread_mutex_unlock(&tree->tree_lock);
        LOG_ERROR("[NPU-TREE] Failed to get device count or no devices");
        return ENPU_FAIL;
    }

    if (device_count > MAX_NPU_PER_NODE) {
        device_count = MAX_NPU_PER_NODE;
    }

    int *logic_ids = (int *)calloc(device_count, sizeof(int));
    if (logic_ids == NULL) {
        pthread_mutex_unlock(&tree->tree_lock);
        return ENPU_FAIL;
    }

    int actual_count = 0;
    ret = adapter->get_device_list(adapter, logic_ids, device_count, &actual_count);
    if (ret != ENPU_SUCCESS) {
        free(logic_ids);
        pthread_mutex_unlock(&tree->tree_lock);
        LOG_ERROR("[NPU-TREE] Failed to get device list");
        return ENPU_FAIL;
    }

    for (int i = 0; i < actual_count; i++) {
        dcmi_device_info_t dev_info = {0};

        ret = adapter->get_device_info(adapter, logic_ids[i], &dev_info);
        if (ret != ENPU_SUCCESS) {
            LOG_DEBUG("[NPU-TREE] Failed to get device info for logic_id %d", logic_ids[i]);
            continue;
        }

        npu_node_t *node = create_node();
        if (node == NULL) {
            free(logic_ids);
            pthread_mutex_unlock(&tree->tree_lock);
            return ENPU_FAIL;
        }

        node->meta.id = logic_ids[i];
        node->meta.phy_id = logic_ids[i]; /* phy_id == logic_id（全局物理编号, 对应 npu-smi 设备号）*/
        node->meta.minor_id = logic_ids[i];
        ret = strncpy_s(node->meta.uuid, MAX_UUID_LEN, dev_info.uuid, strnlen(dev_info.uuid, MAX_UUID_LEN - 1));
        if (ret != 0) {
            LOG_ERROR("[NPU-TREE] strncpy_s uuid failed, ret=%d", ret);
            free(node);
            free(logic_ids);
            pthread_mutex_unlock(&tree->tree_lock);
            return ENPU_FAIL;
        }
        ret = snprintf_s(node->meta.minor_name, MAX_NAME_LEN, MAX_NAME_LEN - 1, "Ascend%d", logic_ids[i]);
        if (ret < 0) {
            LOG_ERROR("[NPU-TREE] snprintf_s minor_name failed, ret=%d", ret);
            free(node);
            free(logic_ids);
            pthread_mutex_unlock(&tree->tree_lock);
            return ENPU_FAIL;
        }
        node->meta.total_memory = (uint64_t)dev_info.total_memory * 1024ULL * 1024ULL;
        ret = strncpy_s(node->meta.card_type, MAX_NAME_LEN, "Ascend910B", strlen("Ascend910B"));
        if (ret != 0) {
            LOG_ERROR("[NPU-TREE] strncpy_s card_type failed, ret=%d", ret);
            free(node);
            free(logic_ids);
            pthread_mutex_unlock(&tree->tree_lock);
            return ENPU_FAIL;
        }

        node->topo_level = NPU_TOPO_DIE;
        node->mask = (1ULL << logic_ids[i]);

        atomic_store(&node->allocatable.aicore_quota, (int)dev_info.total_aicore);
        atomic_store(&node->allocatable.hbm_quota, dev_info.total_memory);
        atomic_store(&node->allocatable.vnpu_count, 0);
        node->allocatable.vnpu_mask = 0xFFFFFFFFFFFFFFFFULL;

        dcmi_die_info_t die_info = {0};
        int die_ret = adapter->get_die_info(adapter, logic_ids[i], &die_info);
        if (die_ret != ENPU_SUCCESS) {
            LOG_ERROR("[NPU-TREE] Failed to get die_info for logic_id=%d via adapter (ret=%d), abort npu_tree_init",
                      logic_ids[i], die_ret);
            free(node);
            free(logic_ids);
            pthread_mutex_unlock(&tree->tree_lock);
            return ENPU_FAIL;
        }
        ret = strncpy_s(node->meta.die_id, DIE_ID_LEN, die_info.die_id, strnlen(die_info.die_id, DIE_ID_LEN - 1));
        if (ret != 0) {
            LOG_ERROR("[NPU-TREE] strncpy_s die_id failed, ret=%d", ret);
            free(node);
            free(logic_ids);
            pthread_mutex_unlock(&tree->tree_lock);
            return ENPU_FAIL;
        }

        tree->leaves[tree->leaf_count++] = node;
        LOG_INFO("[NPU-TREE] npu_tree: built DIE phy_id=%d die_id=%s hbm=%luMB aicore=%d", node->meta.phy_id,
                 node->meta.die_id, (unsigned long)dev_info.total_memory, dev_info.total_aicore);
        if (logic_ids[i] < MAX_NPU_PER_NODE) {
            tree->query_table[logic_ids[i]] = node;
        }

        /* 预留能力: DIE→CARD 层级关系（card_nodes 数组 / parent 链）由后续交付版本补充, 
         * 当前 DIE 节点 parent 保持 NULL, mask 汇聚逻辑暂不生效.  */
    }

    build_tree_hierarchy(tree);

    tree->query_size = tree->leaf_count;
    atomic_store(&tree->initialized, true);

    free(logic_ids);
    pthread_mutex_unlock(&tree->tree_lock);

    LOG_DEBUG("[NPU-TREE] NPU tree initialized with adapter, %d devices", tree->leaf_count);
    return ENPU_SUCCESS;
}

int npu_tree_destroy(npu_tree_t *tree)
{
    CHECK_NULL_RET(tree, ENPU_INVALID_PARAM);

    pthread_mutex_lock(&tree->tree_lock);

    destroy_node(tree->root);

    if (tree->leaves != NULL) {
        free(tree->leaves);
    }
    if (tree->die_nodes != NULL) {
        free(tree->die_nodes);
    }
    if (tree->query_table != NULL) {
        free(tree->query_table);
    }

    atomic_store(&tree->initialized, false);

    pthread_mutex_unlock(&tree->tree_lock);
    pthread_mutex_destroy(&tree->tree_lock);

    free(tree);

    LOG_DEBUG("[NPU-TREE] NPU tree destroyed");
    return ENPU_SUCCESS;
}

npu_node_t *npu_tree_query(npu_tree_t *tree, const char *minor_name)
{
    if (tree == NULL || minor_name == NULL) {
        return NULL;
    }

    if (!atomic_load(&tree->initialized)) {
        return NULL;
    }

    pthread_mutex_lock(&tree->tree_lock);
    npu_node_t *result = NULL;

    for (int i = 0; i < tree->leaf_count; i++) {
        if (strncmp(tree->leaves[i]->meta.minor_name, minor_name, MAX_NAME_LEN) == 0) {
            result = tree->leaves[i];
            break;
        }
    }

    pthread_mutex_unlock(&tree->tree_lock);
    return result;
}

npu_node_t *npu_tree_get_leaf(npu_tree_t *tree, int phy_id)
{
    if (tree == NULL || phy_id < 0) {
        return NULL;
    }

    if (!atomic_load(&tree->initialized)) {
        return NULL;
    }

    pthread_mutex_lock(&tree->tree_lock);
    npu_node_t *result = NULL;

    for (int i = 0; i < tree->leaf_count; i++) {
        if (tree->leaves[i]->meta.phy_id == phy_id) {
            result = tree->leaves[i];
            break;
        }
    }

    pthread_mutex_unlock(&tree->tree_lock);
    return result;
}

int npu_tree_mark_occupied(npu_tree_t *tree, npu_node_t *node, int vnpu_id, allocation_t *alloc)
{
    CHECK_NULL_RET(tree, ENPU_INVALID_PARAM);
    CHECK_NULL_RET(node, ENPU_INVALID_PARAM);
    CHECK_NULL_RET(alloc, ENPU_INVALID_PARAM);

    if (vnpu_id < 0 || vnpu_id >= MAX_VNPU_PER_DIE) {
        LOG_ERROR("[NPU-TREE] Invalid vnpu_id: %d", vnpu_id);
        return ENPU_INVALID_PARAM;
    }

    pthread_mutex_lock(&node->lock);

    uint64_t bit_mask = (1ULL << vnpu_id);
    if ((node->allocatable.vnpu_mask & bit_mask) == 0) {
        LOG_ERROR("[NPU-TREE] vNPU %d already occupied on device %d", vnpu_id, node->meta.id);
        pthread_mutex_unlock(&node->lock);
        return ENPU_ALREADY_EXISTS;
    }

    node->allocatable.vnpu_mask &= ~bit_mask;

    atomic_fetch_sub(&node->allocatable.aicore_quota, alloc->aicore_quota);
    uint64_t new_hbm = atomic_load(&node->allocatable.hbm_quota) - alloc->hbm_quota;
    atomic_store(&node->allocatable.hbm_quota, new_hbm);
    atomic_fetch_add(&node->allocatable.vnpu_count, 1);

    if (node->parent != NULL) {
        pthread_mutex_lock(&node->parent->lock);
        node->parent->mask &= ~node->mask;
        pthread_mutex_unlock(&node->parent->lock);
    }

    pthread_mutex_unlock(&node->lock);

    LOG_DEBUG("[NPU-TREE] vNPU %d marked occupied on device %d", vnpu_id, node->meta.id);
    return ENPU_SUCCESS;
}

int npu_tree_mark_free(npu_tree_t *tree, npu_node_t *node, int vnpu_id)
{
    CHECK_NULL_RET(tree, ENPU_INVALID_PARAM);
    CHECK_NULL_RET(node, ENPU_INVALID_PARAM);

    if (vnpu_id < 0 || vnpu_id >= MAX_VNPU_PER_DIE) {
        LOG_ERROR("[NPU-TREE] Invalid vnpu_id: %d", vnpu_id);
        return ENPU_INVALID_PARAM;
    }

    pthread_mutex_lock(&node->lock);

    uint64_t bit_mask = (1ULL << vnpu_id);
    if ((node->allocatable.vnpu_mask & bit_mask) != 0) {
        LOG_DEBUG("[NPU-TREE] vNPU %d already free on device %d", vnpu_id, node->meta.id);
        pthread_mutex_unlock(&node->lock);
        return ENPU_SUCCESS;
    }

    node->allocatable.vnpu_mask |= bit_mask;
    atomic_fetch_sub(&node->allocatable.vnpu_count, 1);

    if (node->parent != NULL) {
        pthread_mutex_lock(&node->parent->lock);
        node->parent->mask |= node->mask;
        pthread_mutex_unlock(&node->parent->lock);
    }

    pthread_mutex_unlock(&node->lock);

    LOG_DEBUG("[NPU-TREE] vNPU %d marked free on device %d", vnpu_id, node->meta.id);
    return ENPU_SUCCESS;
}

int npu_tree_mark_vnpu_occupied(npu_tree_t *tree, npu_node_t *node, int vnpu_id)
{
    CHECK_NULL_RET(tree, ENPU_INVALID_PARAM);
    CHECK_NULL_RET(node, ENPU_INVALID_PARAM);

    if (vnpu_id < 0 || vnpu_id >= MAX_VNPU_PER_DIE) {
        LOG_ERROR("[NPU-TREE] Invalid vnpu_id: %d", vnpu_id);
        return ENPU_INVALID_PARAM;
    }

    pthread_mutex_lock(&node->lock);

    uint64_t bit_mask = (1ULL << vnpu_id);
    if ((node->allocatable.vnpu_mask & bit_mask) == 0) {
        LOG_ERROR("[NPU-TREE] vNPU %d already occupied on device %d", vnpu_id, node->meta.id);
        pthread_mutex_unlock(&node->lock);
        return ENPU_ALREADY_EXISTS;
    }

    node->allocatable.vnpu_mask &= ~bit_mask;

    if (node->parent != NULL) {
        pthread_mutex_lock(&node->parent->lock);
        node->parent->mask &= ~node->mask;
        pthread_mutex_unlock(&node->parent->lock);
    }

    pthread_mutex_unlock(&node->lock);

    LOG_DEBUG("[NPU-TREE] vNPU %d topology marked occupied on device %d", vnpu_id, node->meta.id);
    return ENPU_SUCCESS;
}

int npu_tree_mark_vnpu_free(npu_tree_t *tree, npu_node_t *node, int vnpu_id)
{
    CHECK_NULL_RET(tree, ENPU_INVALID_PARAM);
    CHECK_NULL_RET(node, ENPU_INVALID_PARAM);

    if (vnpu_id < 0 || vnpu_id >= MAX_VNPU_PER_DIE) {
        LOG_ERROR("[NPU-TREE] Invalid vnpu_id: %d", vnpu_id);
        return ENPU_INVALID_PARAM;
    }

    pthread_mutex_lock(&node->lock);

    uint64_t bit_mask = (1ULL << vnpu_id);
    if ((node->allocatable.vnpu_mask & bit_mask) != 0) {
        LOG_DEBUG("[NPU-TREE] vNPU %d already free on device %d", vnpu_id, node->meta.id);
        pthread_mutex_unlock(&node->lock);
        return ENPU_SUCCESS;
    }

    node->allocatable.vnpu_mask |= bit_mask;

    if (node->parent != NULL) {
        pthread_mutex_lock(&node->parent->lock);
        node->parent->mask |= node->mask;
        pthread_mutex_unlock(&node->parent->lock);
    }

    pthread_mutex_unlock(&node->lock);

    LOG_DEBUG("[NPU-TREE] vNPU %d topology marked free on device %d", vnpu_id, node->meta.id);
    return ENPU_SUCCESS;
}

int npu_tree_update_quota(npu_tree_t *tree, npu_node_t *node, int32_t aicore_delta, int64_t hbm_delta)
{
    CHECK_NULL_RET(tree, ENPU_INVALID_PARAM);
    CHECK_NULL_RET(node, ENPU_INVALID_PARAM);

    pthread_mutex_lock(&node->lock);

    if (aicore_delta != 0) {
        atomic_fetch_add(&node->allocatable.aicore_quota, aicore_delta);
    }

    if (hbm_delta != 0) {
        uint64_t current_hbm = atomic_load(&node->allocatable.hbm_quota);
        int64_t signed_delta = hbm_delta;
        uint64_t new_hbm = (signed_delta >= 0) ? (current_hbm + (uint64_t)signed_delta) :
                                                 (current_hbm - (uint64_t)(-signed_delta));
        atomic_store(&node->allocatable.hbm_quota, new_hbm);
    }

    pthread_mutex_unlock(&node->lock);

    LOG_DEBUG("[NPU-TREE] Quota updated on device %d: aicore=%d, hbm=%lld", node->meta.id, aicore_delta,
              (long long)hbm_delta);

    return ENPU_SUCCESS;
}

int npu_tree_attach_policy(npu_tree_t *tree, npu_node_t *node, int vnpu_id, int32_t sched_policy, int32_t priority)
{
    CHECK_NULL_RET(tree, ENPU_INVALID_PARAM);
    CHECK_NULL_RET(node, ENPU_INVALID_PARAM);

    if (vnpu_id < 0 || vnpu_id >= MAX_VNPU_PER_DIE) {
        LOG_ERROR("[NPU-TREE] Invalid vnpu_id: %d", vnpu_id);
        return ENPU_INVALID_PARAM;
    }

    pthread_mutex_lock(&node->lock);

    if (node->policy_entries == NULL) {
        node->policy_entries = (vnpu_policy_entry_t *)calloc(MAX_VNPU_PER_DIE, sizeof(vnpu_policy_entry_t));
        if (node->policy_entries == NULL) {
            LOG_ERROR("[NPU-TREE] Failed to allocate policy_entries");
            pthread_mutex_unlock(&node->lock);
            return ENPU_FAIL;
        }
    }

    if (node->policy_entry_count < MAX_VNPU_PER_DIE) {
        node->policy_entries[node->policy_entry_count].vnpu_id = vnpu_id;
        node->policy_entries[node->policy_entry_count].sched_policy = sched_policy;
        node->policy_entries[node->policy_entry_count].priority = priority;
        node->policy_entry_count++;
        atomic_fetch_add(&node->allocatable.vnpu_count, 1);
    }

    pthread_mutex_unlock(&node->lock);

    LOG_DEBUG("[NPU-TREE] Policy attached: vnpu=%d, policy=%d on device %d", vnpu_id, sched_policy, node->meta.id);
    return ENPU_SUCCESS;
}

int npu_tree_detach_policy(npu_tree_t *tree, npu_node_t *node, int vnpu_id)
{
    CHECK_NULL_RET(tree, ENPU_INVALID_PARAM);
    CHECK_NULL_RET(node, ENPU_INVALID_PARAM);

    if (vnpu_id < 0 || vnpu_id >= MAX_VNPU_PER_DIE) {
        LOG_ERROR("[NPU-TREE] Invalid vnpu_id: %d", vnpu_id);
        return ENPU_INVALID_PARAM;
    }

    pthread_mutex_lock(&node->lock);

    int found_idx = -1;
    for (int i = 0; i < node->policy_entry_count; i++) {
        if (node->policy_entries[i].vnpu_id == vnpu_id) {
            found_idx = i;
            break;
        }
    }

    if (found_idx >= 0) {
        for (int i = found_idx; i < node->policy_entry_count - 1; i++) {
            node->policy_entries[i] = node->policy_entries[i + 1];
        }
        node->policy_entry_count--;
        atomic_fetch_sub(&node->allocatable.vnpu_count, 1);
    }

    pthread_mutex_unlock(&node->lock);

    LOG_DEBUG("[NPU-TREE] Policy detached: vnpu=%d on device %d", vnpu_id, node->meta.id);
    return ENPU_SUCCESS;
}

int npu_tree_get_available_vnpu_id(npu_node_t *node)
{
    CHECK_NULL_RET(node, ENPU_INVALID_PARAM);

    pthread_mutex_lock(&node->lock);

    if (node->allocatable.vnpu_mask == 0) {
        pthread_mutex_unlock(&node->lock);
        return ENPU_NO_RESOURCE;
    }

    for (int i = 0; i < MAX_VNPU_PER_DIE; i++) {
        if (node->allocatable.vnpu_mask & (1ULL << i)) {
            pthread_mutex_unlock(&node->lock);
            return i;
        }
    }

    pthread_mutex_unlock(&node->lock);
    return ENPU_NO_RESOURCE;
}

uint64_t npu_tree_get_available_aicore(npu_node_t *node)
{
    if (node == NULL) {
        return 0;
    }
    return (uint64_t)atomic_load(&node->allocatable.aicore_quota);
}

uint64_t npu_tree_get_available_hbm(npu_node_t *node)
{
    if (node == NULL) {
        return 0;
    }
    return atomic_load(&node->allocatable.hbm_quota);
}

int npu_tree_save_checkpoint(npu_tree_t *tree, const char *path)
{
    CHECK_NULL_RET(tree, ENPU_INVALID_PARAM);
    CHECK_NULL_RET(path, ENPU_INVALID_PARAM);

    if (!atomic_load(&tree->initialized)) {
        LOG_ERROR("[NPU-TREE] Tree not initialized");
        return ENPU_FAIL;
    }

    LOG_DEBUG("[NPU-TREE] Checkpoint save to %s (stub)", path);
    return ENPU_SUCCESS;
}

int npu_tree_load_checkpoint(npu_tree_t *tree, const char *path)
{
    CHECK_NULL_RET(tree, ENPU_INVALID_PARAM);
    CHECK_NULL_RET(path, ENPU_INVALID_PARAM);

    LOG_DEBUG("[NPU-TREE] Checkpoint load from %s (stub)", path);
    return ENPU_SUCCESS;
}

int32_t npu_node_get_available_aicore(npu_node_t *node)
{
    if (node == NULL) {
        return -1;
    }
    return atomic_load(&node->allocatable.aicore_quota);
}

uint64_t npu_node_get_available_hbm(npu_node_t *node)
{
    if (node == NULL) {
        return 0;
    }

    return atomic_load(&node->allocatable.hbm_quota);
}

int npu_node_get_vnpu_count(npu_node_t *node)
{
    if (node == NULL) {
        return -1;
    }
    return atomic_load(&node->allocatable.vnpu_count);
}

int32_t npu_node_get_phy_id(npu_node_t *node)
{
    if (node == NULL) {
        return -1;
    }
    return node->meta.phy_id;
}

const char *npu_node_get_die_id(npu_node_t *node)
{
    if (node == NULL) {
        return NULL;
    }
    return node->meta.die_id;
}

int32_t npu_node_get_minor_id(npu_node_t *node)
{
    if (node == NULL) {
        return -1;
    }
    return node->meta.minor_id;
}

const char *npu_node_get_uuid(npu_node_t *node)
{
    if (node == NULL) {
        return NULL;
    }
    return node->meta.uuid;
}

uint64_t npu_node_get_total_memory(npu_node_t *node)
{
    if (node == NULL) {
        return 0;
    }
    return node->meta.total_memory;
}

npu_topo_level_t npu_node_get_topo_level(npu_node_t *node)
{
    if (node == NULL) {
        return NPU_TOPO_SYS;
    }
    return node->topo_level;
}

npu_node_t *npu_node_get_parent(npu_node_t *node)
{
    if (node == NULL) {
        return NULL;
    }
    return node->parent;
}

const char *npu_node_get_card_type(npu_node_t *node)
{
    if (node == NULL) {
        return NULL;
    }
    return node->meta.card_type;
}

int32_t npu_node_get_id(npu_node_t *node)
{
    if (node == NULL) {
        return -1;
    }
    return node->meta.id;
}

const char *npu_node_get_minor_name(npu_node_t *node)
{
    if (node == NULL) {
        return NULL;
    }
    return node->meta.minor_name;
}

int32_t npu_node_get_aicore_quota(npu_node_t *node)
{
    if (node == NULL) {
        return 0;
    }
    return atomic_load(&node->allocatable.aicore_quota);
}

uint64_t npu_node_get_hbm_quota(npu_node_t *node)
{
    if (node == NULL) {
        return 0;
    }
    return atomic_load(&node->allocatable.hbm_quota);
}

uint64_t npu_node_get_vnpu_mask(npu_node_t *node)
{
    if (node == NULL) {
        return 0;
    }
    return node->allocatable.vnpu_mask;
}

int npu_node_validate_policy(npu_node_t *node, int32_t sched_policy)
{
    if (node == NULL) {
        return ENPU_INVALID_PARAM;
    }

    pthread_mutex_lock(&node->lock);

    for (int i = 0; i < node->policy_entry_count; i++) {
        if (node->policy_entries[i].priority == VNPU_PRIORITY_HIGH) {
            continue;
        }
        if (node->policy_entries[i].sched_policy != sched_policy) {
            pthread_mutex_unlock(&node->lock);
            return ENPU_FAIL;
        }
    }

    pthread_mutex_unlock(&node->lock);
    return ENPU_SUCCESS;
}

int npu_node_get_available_vnpu_id(npu_node_t *node)
{
    return npu_tree_get_available_vnpu_id(node);
}
