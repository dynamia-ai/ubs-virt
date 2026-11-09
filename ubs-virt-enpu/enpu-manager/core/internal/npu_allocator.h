/*
 * Copyright (c) Huawei Technologies Co., Ltd. 2025-2026. All rights reserved.
 * enpu-manager is licensed under Mulan PSL v2.
 *
 * npu_allocator.h - INTERNAL IMPLEMENTATION for enpu_manager
 *
 * This header is internal to the enpu_manager subsystem. It provides
 * allocator interface for internal components. External callers
 * should use enpu_manager.h instead.
 *
 * Location: core/internal/ (not part of public API)
 */

#ifndef __NPU_ALLOCATOR_INTERNAL_H__
#define __NPU_ALLOCATOR_INTERNAL_H__

#include <pthread.h>
#include "../include/enpu_manager.h"
#include "allocation.h"
#include "common.h"
#include "dcmi_stub_adapter.h"
#include "npu_allocator_internal.h"

struct rollback_log;
typedef struct rollback_log rollback_log_t;

struct evaluator_registry;
typedef struct evaluator_registry evaluator_registry_t;

struct npu_tree;
struct config_manager;
struct shm_manager;
struct allocation_registry;
struct shm_state;
typedef struct npu_tree npu_tree_t;
typedef struct config_manager config_manager_t;
typedef struct shm_manager shm_manager_t;
typedef struct allocation_registry allocation_registry_t;
typedef struct shm_state shm_state_t;

typedef enum
{
    ALLOC_MODE_SHARE = 0,
    ALLOC_MODE_VNPU = 1,
    ALLOC_MODE_FRAGMENT = 2,
} alloc_mode_t;

typedef struct allocator_config {
    char config_base_path[MAX_PATH_LEN];
    char checkpoint_path[MAX_PATH_LEN];
    char evaluator_name[MAX_NAME_LEN];
    char share_strategy[MAX_NAME_LEN];
    bool use_dcmi_stub;
    dcmi_stub_config_t dcmi_stub;
} allocator_config_t;

struct npu_allocator;

#if defined(__cplusplus)
extern "C" {
#endif

npu_allocator_t *npu_allocator_create(const allocator_config_t *config);
int npu_allocator_init(npu_allocator_t *alloc);
int npu_allocator_destroy(npu_allocator_t *alloc);

int npu_allocator_allocate(npu_allocator_t *alloc, alloc_request_t *req, alloc_response_t *resp);
int npu_allocator_release_by_id(npu_allocator_t *alloc, const char *allocation_id);
int npu_allocator_release(npu_allocator_t *alloc, const char *pod_uid, const char *container_name);
int npu_allocator_release_by_vnpu(npu_allocator_t *alloc, int phy_id, int vnpu_id);

/* K8s 路径专用: 外部组件（mindcluster）负责选卡与容器生命周期, enpu-manager 只做 shm 清理 */

/* 释放 vnpu 的 shm 资源（K8s 路径）. entry 不存在时返回 ENPU_SUCCESS. */
int npu_allocator_release_external_vnpu(npu_allocator_t *alloc, int32_t phy_id, int32_t vnpu_id);

int npu_allocator_query_allocations(npu_allocator_t *alloc, const allocation_query_t *query, allocation_t *result,
                                    int *count);

int npu_allocator_query_devices(npu_allocator_t *alloc, int32_t phy_id, npu_meta_t *meta,
                                npu_allocatable_t *allocatable, int *count);

int npu_allocator_get_swap_state(npu_allocator_t *alloc, int32_t phy_id, int vnpu_id, bool *swapped, uint64_t *offset,
                                 uint64_t *size);
int npu_allocator_set_swap_state(npu_allocator_t *alloc, int32_t phy_id, int vnpu_id, uint64_t offset, uint64_t size);
int npu_allocator_clear_swap(npu_allocator_t *alloc, int32_t phy_id, int vnpu_id);
int npu_allocator_write_swap_cmd(npu_allocator_t *alloc, int32_t phy_id, const char *pod_uid, int vnpu_id, int action,
                                 uint8_t flag);

int npu_allocator_recover(npu_allocator_t *alloc);
int npu_allocator_checkpoint(npu_allocator_t *alloc);

allocation_registry_t *npu_allocator_get_registry(npu_allocator_t *alloc);

// shm_manager is internal to npu_allocator.
// Do NOT expose it to external callers.
// Use npu_allocator_get_swap_state(), npu_allocator_set_swap_state(),
// npu_allocator_foreach_swapped_vnpu() instead.

typedef void (*swapped_vnpu_callback_t)(int phy_id, int vnpu_id, uint64_t swap_size, void *user_data);

int npu_allocator_foreach_swapped_vnpu(npu_allocator_t *alloc, swapped_vnpu_callback_t callback, void *user_data);

shm_state_t *npu_allocator_get_shm_state(npu_allocator_t *alloc, int phy_id);

shm_manager_t *npu_allocator_get_shm_manager(npu_allocator_t *alloc);

/* 节点级总 HBM（所有 DIE 之和）, 单位 MB */
uint64_t npu_allocator_get_total_hbm_mb(npu_allocator_t *alloc);

/* per-die HBM 总量, 单位 MB. out_hbm_mb 长度需 ≥ MAX_NPU_PER_NODE */
int npu_allocator_get_per_die_hbm_mb(npu_allocator_t *alloc, uint64_t *out_hbm_mb, int max_count);

/* per-die 超分比例读写访问器（封装内部 oversub_ratio 成员） */
int npu_allocator_set_oversub_ratio(npu_allocator_t *alloc, int die_index, double ratio);
double npu_allocator_get_oversub_ratio(npu_allocator_t *alloc, int die_index);

int npu_allocator_get_last_kernel_time_ns(npu_allocator_t *alloc, int phy_id, int vnpu_id,
                                          uint64_t *last_kernel_time_ns);

int parse_hbm_request_limit(alloc_request_t *req, uint64_t *hbm_request, uint64_t *hbm_limit, char *error_msg);

#if defined(__cplusplus)
}
#endif

#endif
