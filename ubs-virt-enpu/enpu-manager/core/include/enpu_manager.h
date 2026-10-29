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

#ifndef __ENPU_MANAGER_H__
#define __ENPU_MANAGER_H__

#include <pthread.h>
#include <stdatomic.h>
#include <stdbool.h>
#include "allocation.h"
#include "common.h"
#include "dcmi_stub_adapter.h"
#include "swap_buffer_manager.h"

struct npu_tree;
struct npu_node;
struct npu_allocator;
struct config_manager;
struct shm_manager;
struct allocation;
struct swap_buffer_manager;
struct swap_resolver;
typedef struct npu_tree npu_tree_t;
typedef struct npu_node npu_node_t;
typedef struct config_manager config_manager_t;
typedef struct shm_manager shm_manager_t;
typedef struct allocation allocation_t;
typedef struct swap_buffer_manager swap_buffer_manager_t;
typedef struct swap_resolver swap_resolver_t;

typedef struct alloc_request {
    char pod_uid[MAX_UUID_LEN];        /* 可选；未指定时默认 pod-<phy_id> */
    char container_name[MAX_NAME_LEN]; /* 可选；未指定时默认 container-<phy_id>-<vnpu_id> */
    int32_t aicore_quota;
    uint64_t hbm_quota;
    uint64_t hbm_limit;
    int32_t sched_policy;
    int32_t swap_priority;
    int32_t vnpu_priority;
    int32_t predicate_phy_id; /* -1=自动选择；>=0直接分配到指定物理卡 */
    char predicate_node[MAX_NAME_LEN];
    uint64_t predicate_time;
    char process_id[MAX_UUID_LEN];
} alloc_request_t;

typedef struct alloc_response {
    int32_t phy_id;
    int32_t vnpu_id;
    char pod_uid[MAX_UUID_LEN];        /* 回显实际使用的 pod_uid（含默认值） */
    char container_name[MAX_NAME_LEN]; /* 回显实际使用的 container_name（含默认值） */
    char die_id[DIE_ID_LEN];
    char shm_id[SHM_ID_LEN];
    int32_t aicore_quota;
    uint64_t hbm_quota;
    uint64_t hbm_limit;
    int32_t sched_policy;
    int32_t vnpu_priority;
    char minor_name[MAX_NAME_LEN];
    int32_t result;
    char error_msg[MAX_PATH_LEN];
} alloc_response_t;

typedef struct allocation_query {
    int32_t phy_id;
    char pod_uid[MAX_UUID_LEN];
    char container_name[MAX_NAME_LEN];
} allocation_query_t;

typedef struct device_query {
    int32_t phy_id;
} device_query_t;

typedef struct enpu_manager_config {
    char config_base_path[MAX_PATH_LEN];
    char checkpoint_path[MAX_PATH_LEN];
    char swap_buffer_path[MAX_PATH_LEN];
    double oversub_ratio[MAX_NPU_PER_NODE];
    int swap_pre_watermark;
    char evaluator_name[MAX_NAME_LEN];
    char share_strategy[MAX_NAME_LEN]; // compact或anti-fragment
    bool use_dcmi_stub;
    dcmi_stub_config_t dcmi_stub;
    uint64_t watchdog_poll_interval_ms;
} enpu_manager_config_t;

typedef enum swap_action
{
    SWAP_ACTION_CLEAN = 0,
    SWAP_ACTION_FORCE_OUT = 1,
    SWAP_ACTION_FORCE_IN = 2,
} swap_action_t;

typedef enum control_op
{
    CONTROL_OP_CHECKPOINT = 0,
    CONTROL_OP_RECOVER = 1,
    CONTROL_OP_HEALTH_CHECK = 2,
} control_op_t;

typedef struct enpu_manager enpu_manager_t;

typedef struct swapped_model_info {
    char pod_uid[MAX_UUID_LEN];
    int vnpu_id;
    int die_id;
    uint64_t swap_size;
} swapped_model_info_t;

typedef struct swap_status_response {
    swap_buffer_status_t buffer_status;
    swapped_model_info_t swapped_models[MAX_VNPU_PER_DIE * MAX_NPU_PER_NODE];
    int swapped_count;
} swap_status_response_t;

typedef struct device_info {
    int32_t phy_id;
    npu_meta_t meta;
    npu_allocatable_t allocatable;
    double oversub_ratio;
} device_info_t;

#if defined(__cplusplus)
extern "C" {
#endif

enpu_manager_t *enpu_manager_create(const enpu_manager_config_t *config);
int enpu_manager_start(enpu_manager_t *mgr);
int enpu_manager_stop(enpu_manager_t *mgr);
int enpu_manager_destroy(enpu_manager_t *mgr);

int enpu_manager_allocate(enpu_manager_t *mgr, alloc_request_t *req, alloc_response_t *resp);
int enpu_manager_release(enpu_manager_t *mgr, const char *pod_uid, const char *container_name);
int enpu_manager_release_all(enpu_manager_t *mgr, int *released_count);

int enpu_manager_query_devices(enpu_manager_t *mgr, const device_query_t *query, device_info_t *devices, int *count);
int enpu_manager_query_allocations(enpu_manager_t *mgr, const allocation_query_t *query, allocation_t *allocations,
                                   int *count);

int enpu_manager_swap_status(enpu_manager_t *mgr, swap_status_response_t *resp);
int enpu_manager_swap_control(enpu_manager_t *mgr, const char *pod_uid, swap_action_t action);

int enpu_manager_control(enpu_manager_t *mgr, control_op_t op);

#if defined(__cplusplus)
}
#endif

#endif
