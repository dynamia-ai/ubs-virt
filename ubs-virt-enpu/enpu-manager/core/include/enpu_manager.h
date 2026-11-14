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
#include <stddef.h>
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
    char allocation_id[ALLOCATION_ID_LEN]; /* Optional; generated when empty. */
    char pod_uid[MAX_UUID_LEN];            /* Optional metadata. */
    char container_name[MAX_NAME_LEN];     /* Optional metadata. */
    int32_t aicore_quota;
    uint64_t hbm_quota;
    uint64_t hbm_limit;
    int32_t sched_policy;
    int32_t swap_order;
    int32_t vnpu_priority;
    int32_t predicate_phy_id; /* -1=自动选择；>=0直接分配到指定物理卡 */
    char predicate_node[MAX_NAME_LEN];
    uint64_t predicate_time;
    char process_id[MAX_UUID_LEN];
} alloc_request_t;

typedef struct alloc_response {
    char allocation_id[ALLOCATION_ID_LEN];
    char config_path[MAX_PATH_LEN];
    int32_t phy_id;
    int32_t vnpu_id;
    char pod_uid[MAX_UUID_LEN];
    char container_name[MAX_NAME_LEN];
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

/* Structured resource-query API. */
typedef enum enpu_resource_query_type
{
    ENPU_RESOURCE_QUERY_ALL = 0,
    ENPU_RESOURCE_QUERY_BY_PHY,
    ENPU_RESOURCE_QUERY_BY_RESOURCE,
    ENPU_RESOURCE_QUERY_BY_ALLOCATION,
    ENPU_RESOURCE_QUERY_BY_METADATA,
} enpu_resource_query_type_t;

typedef struct enpu_resource_query {
    enpu_resource_query_type_t type;
    union {
        int32_t phy_id;
        struct {
            int32_t phy_id;
            int32_t vnpu_id;
        } resource;
        char allocation_id[ALLOCATION_ID_LEN];
        struct {
            char pod_uid[MAX_UUID_LEN];
            char container_name[MAX_NAME_LEN];
        } metadata;
    } selector;
    bool detail;
} enpu_resource_query_t;

typedef enum enpu_resource_metric_status
{
    ENPU_RESOURCE_METRIC_OK = 0,
    ENPU_RESOURCE_METRIC_IDLE,
    ENPU_RESOURCE_METRIC_UNSUPPORTED,
    ENPU_RESOURCE_METRIC_UNAVAILABLE,
    ENPU_RESOURCE_METRIC_STALE,
    ENPU_RESOURCE_METRIC_INCONSISTENT,
    ENPU_RESOURCE_METRIC_STATS_SHM_MISSING,
    ENPU_RESOURCE_METRIC_STATS_VERSION_MISMATCH,
    ENPU_RESOURCE_METRIC_STATS_UNAVAILABLE,
    ENPU_RESOURCE_METRIC_AVG_DURATION_MISSING,
    ENPU_RESOURCE_METRIC_DCMI_UNAVAILABLE,
    ENPU_RESOURCE_METRIC_VNPU_ID_OUT_OF_RANGE,
} enpu_resource_metric_status_t;

typedef struct enpu_resource_metric {
    double value; /* 百分数，0～100；仅 OK/IDLE 有效 */
    enpu_resource_metric_status_t status;
} enpu_resource_metric_t;

typedef struct enpu_resource_quota {
    int32_t aicore_percent; /* 仅 core_limit_enabled=true 时表示限额 */
    schedule_policy_t scheduling_policy;
    bool core_limit_enabled; /* best-effort 为 false，不把 quota=0 解释为零算力 */
} enpu_resource_quota_t;

typedef struct enpu_resource_hbm_usage {
    uint64_t used_bytes; /* 仅 status=OK/IDLE 时有效，合法用量可以为 0 */
    enpu_resource_metric_status_t status;
} enpu_resource_hbm_usage_t;

typedef struct enpu_resource_hbm {
    uint64_t request_bytes; /* allocation 配额，不依赖 usage 采集状态 */
    uint64_t limit_bytes;
    enpu_resource_hbm_usage_t usage;
} enpu_resource_hbm_t;

typedef struct enpu_resource_physical {
    enpu_resource_metric_t npu_utilization;
    enpu_resource_metric_t aicore_utilization;
    enpu_resource_metric_t aicpu_utilization;
    enpu_resource_metric_t ctrlcpu_utilization;
    enpu_resource_metric_t cube_utilization;
    enpu_resource_metric_t vector_utilization;
} enpu_resource_physical_t;

typedef struct enpu_resource_vnpu {
    enpu_resource_metric_t utilization;
    enpu_resource_metric_t aicore_utilization;
} enpu_resource_vnpu_t;

typedef struct enpu_resource_metadata {
    char pod_uid[MAX_UUID_LEN];
    char container_name[MAX_NAME_LEN];
} enpu_resource_metadata_t;

typedef struct enpu_resource_metric_detail {
    int source_result;                /* 数据源返回码或本地诊断码；不能代替业务 status */
    uint64_t sampled_at_monotonic_ns; /* 对应数据源的采样/快照时间，无样本为 0 */
} enpu_resource_metric_detail_t;

typedef struct enpu_resource_physical_detail {
    enpu_resource_metric_detail_t npu_utilization;
    enpu_resource_metric_detail_t aicore_utilization;
    enpu_resource_metric_detail_t aicpu_utilization;
    enpu_resource_metric_detail_t ctrlcpu_utilization;
    enpu_resource_metric_detail_t cube_utilization;
    enpu_resource_metric_detail_t vector_utilization;
} enpu_resource_physical_detail_t;

typedef struct enpu_resource_vnpu_detail {
    enpu_resource_metric_detail_t utilization;
    enpu_resource_metric_detail_t aicore_utilization;
} enpu_resource_vnpu_detail_t;

typedef struct enpu_resource_detail {
    uint32_t window_ms;       /* vNPU block 统计窗口，不代表其他指标采样周期 */
    uint64_t collected_at_ns; /* 查询采集时间，CLOCK_MONOTONIC */
    char die_id[DIE_ID_LEN];
    char shm_id[SHM_ID_LEN];
    uint64_t sum_block_dim;
    uint64_t launch_count;
    uint64_t avg_kernel_duration_ns;
    double work;
    double weight;
    int hbm_source_result;                /* HBM 原始 DCMI 返回码或负 errno，不能代替 usage.status */
    uint64_t hbm_sampled_at_monotonic_ns; /* HBM 最近一次成功采集时间，无样本为 0 */
    enpu_resource_physical_detail_t physical;
    enpu_resource_vnpu_detail_t vnpu;
} enpu_resource_detail_t;

typedef struct enpu_resource_info {
    char allocation_id[ALLOCATION_ID_LEN];
    int32_t phy_id;
    int32_t vnpu_id;
    enpu_resource_metadata_t metadata;
    enpu_resource_quota_t quota;
    enpu_resource_hbm_t hbm;
    enpu_resource_physical_t physical;
    enpu_resource_vnpu_t vnpu;
    enpu_resource_detail_t *detail; /* 未请求 detail 时为 NULL */
} enpu_resource_info_t;

typedef struct enpu_resource_response {
    enpu_resource_info_t *items;
    size_t count;
} enpu_resource_response_t;

#if defined(__cplusplus)
extern "C" {
#endif

enpu_manager_t *enpu_manager_create(const enpu_manager_config_t *config);
int enpu_manager_start(enpu_manager_t *mgr);
int enpu_manager_stop(enpu_manager_t *mgr);
int enpu_manager_destroy(enpu_manager_t *mgr);

int enpu_manager_allocate(enpu_manager_t *mgr, alloc_request_t *req, alloc_response_t *resp);
int enpu_manager_release_by_id(enpu_manager_t *mgr, const char *allocation_id);
int enpu_manager_release(enpu_manager_t *mgr, const char *pod_uid, const char *container_name);
int enpu_manager_release_all(enpu_manager_t *mgr, int *released_count);

int enpu_manager_query_devices(enpu_manager_t *mgr, const device_query_t *query, device_info_t *devices, int *count);
int enpu_manager_query_allocations(enpu_manager_t *mgr, const allocation_query_t *query, allocation_t *allocations,
                                   int *count);
char *enpu_manager_get_config_path(enpu_manager_t *mgr, const char *allocation_id, char *path, int path_size);
/* In-process, synchronous query. query=NULL selects all allocated resources without detail.
 * response must be zero-initialized or previously freed. On failure it is reset to empty.
 * ENPU_SUCCESS does not imply that every metric is available; check each metric status.
 * The manager must remain alive for the call; callers own separate response objects.
 */
int enpu_query_resources(enpu_manager_t *mgr, const enpu_resource_query_t *query, enpu_resource_response_t *response);
/* Frees all nested allocations, clears response, accepts NULL and repeated calls. */
void enpu_free_resource_response(enpu_resource_response_t *response);
/* Returns a static string; unknown enum values map to "unknown". Do not free it. */
const char *enpu_resource_metric_status_string(enpu_resource_metric_status_t status);

int enpu_manager_swap_status(enpu_manager_t *mgr, swap_status_response_t *resp);
int enpu_manager_swap_control(enpu_manager_t *mgr, const char *pod_uid, swap_action_t action);

int enpu_manager_control(enpu_manager_t *mgr, control_op_t op);

#if defined(__cplusplus)
}
#endif

#endif
