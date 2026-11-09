/*
 * Copyright (c) Huawei Technologies Co., Ltd. 2025-2026. All rights reserved.
 * enpu-manager is licensed under Mulan PSL v2.
 *
 * rollback_log.h - INTERNAL IMPLEMENTATION for npu_allocator
 *
 * This header is internal to the allocator subsystem. It provides
 * rollback functionality for allocation operations. External callers
 * should not directly use these functions - they are invoked by
 * npu_allocator.c during allocation failures.
 *
 * Location: core/internal/ (not part of public API)
 */

#ifndef __ROLLBACK_LOG_H__
#define __ROLLBACK_LOG_H__

#include "common.h"
#include "npu_allocator_internal.h"

struct npu_allocator;

#define MAX_STEPS 16

typedef enum
{
    STEP_MARK_OCCUPIED,
    STEP_UPDATE_QUOTA,
    STEP_ATTACH_POLICY,
    STEP_CREATE_SHM,
    STEP_WRITE_CONFIG,
    STEP_SAVE_CHECKPOINT
} rollback_step_type_t;

typedef struct rollback_step {
    rollback_step_type_t type;
    int32_t phy_id;
    int vnpu_id;
    int32_t aicore_delta;
    int64_t hbm_delta;
    int32_t sched_policy;
    char allocation_id[ALLOCATION_ID_LEN];
    char shm_id[SHM_ID_LEN];
} rollback_step_t;

typedef struct rollback_log {
    rollback_step_t steps[MAX_STEPS];
    int step_count;
} rollback_log_t;

#if defined(__cplusplus)
extern "C" {
#endif

rollback_log_t *rollback_log_create(void);
void rollback_log_destroy(rollback_log_t *log);

int rollback_log_append(rollback_log_t *log, rollback_step_type_t type, int32_t phy_id, int vnpu_id,
                        int32_t aicore_delta, int64_t hbm_delta, int32_t sched_policy, const char *allocation_id,
                        const char *shm_id);

int rollback_log_rollback(rollback_log_t *log, npu_allocator_t *alloc);

#if defined(__cplusplus)
}
#endif

#endif
