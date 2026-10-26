/*
 * Copyright (c) Huawei Technologies Co., Ltd. 2025-2026. All rights reserved.
 * enpu-manager is licensed under Mulan PSL v2.
 */

#ifndef __ALLOCATION_H__
#define __ALLOCATION_H__

#include <stddef.h>
#include <stdint.h>
#include "common.h"

typedef struct allocation {
    char pod_uid[MAX_UUID_LEN];
    char container_name[MAX_NAME_LEN];
    int32_t phy_id;
    int32_t vnpu_id;
    char die_id[DIE_ID_LEN];
    char shm_id[DIE_ID_LEN];
    int32_t aicore_quota;
    uint64_t hbm_quota;
    uint64_t hbm_limit;
    int32_t sched_policy;
    int32_t swap_priority;
    int32_t priority;
    uint64_t create_time_ns;
    uint64_t update_time_ns;
    uint64_t swap_offset;
    uint64_t swap_size;
    bool swapped;
} allocation_t;

typedef struct storage_adapter storage_adapter_t;
typedef struct allocation_registry allocation_registry_t;

allocation_registry_t *allocation_registry_create(void);
allocation_registry_t *allocation_registry_create_with_adapter(storage_adapter_t *adapter);
int allocation_registry_destroy(allocation_registry_t *registry);
int allocation_registry_register(allocation_registry_t *registry, const allocation_t *allocation);
int allocation_registry_release(allocation_registry_t *registry, const char *pod_uid, const char *container_name);
int allocation_registry_get(allocation_registry_t *registry, const char *pod_uid, const char *container_name,
                            allocation_t *allocation);
int allocation_registry_list(allocation_registry_t *registry, allocation_t *allocations, int *count);
int allocation_registry_get_by_phy_id_vnpu_id(allocation_registry_t *registry, int32_t phy_id, int32_t vnpu_id,
                                              allocation_t *allocation);
int allocation_registry_list_by_phy_id(allocation_registry_t *registry, int32_t phy_id, allocation_t *allocations,
                                       int *count);
int allocation_registry_save(allocation_registry_t *registry);
int allocation_registry_recover(allocation_registry_t *registry);
int allocation_registry_update_shm_id(allocation_registry_t *registry, const char *pod_uid, const char *container_name,
                                      const char *new_shm_id);

#endif
