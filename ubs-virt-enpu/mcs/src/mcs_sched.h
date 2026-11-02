/*
 * Copyright (c) Huawei Technologies Co., Ltd. 2026-2026. All rights reserved.
 * ubs-virt-enpu is licensed under Mulan PSL v2.
 * You can use this software according to the terms and conditions of the Mulan PSL v2.
 * You may obtain a copy of Mulan PSL v2 at:
 *          http://license.coscl.org.cn/MulanPSL2
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND,
 * EITHER EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT,
 * MERCHANTABILITY OR FIT FOR A PARTICULAR PURPOSE.
 * See the Mulan PSL v2 for more details.
 */

#ifndef MCS_SCHED_H
#define MCS_SCHED_H

#include <pthread.h>
#include <stdint.h>
#include "mcs_config.h"

#if defined(__cplusplus)
extern "C" {
#endif

/* MCS_MAX_VNPU is defined in mcs_config.h (single source). */

/* Layout version 2: added high_launch_count. Bump whenever the shared-state
 * layout changes so stale shm files are re-initialized instead of being
 * interpreted with garbage in the new fields. */
#define MCS_MAGIC_INITIALIZING 0x4D435300U /* "MCS\0" */
#define MCS_MAGIC_INITIALIZED 0x4D435301U  /* "MCS\1" */
#define MCS_NO_TASK_TIMEOUT_NS 5000000ULL  /* 5ms timeout for detecting idle vNPU */
#define MCS_WAIT_SLEEP_NS 100000ULL        /* 100us sleep when polling */
#define MCS_HIGH_LAUNCH_BURST_DEFAULT \
    300ULL /* anti-starvation: high-priority launches that entitle one normal launch */

typedef struct {
    pthread_mutex_t mcs_mutex;
    uint64_t last_kernel_time_ns[MCS_MAX_VNPU];
    /* Anti-starvation: cumulative count of high-priority kernel launches
     * since the last normal-priority launch. When it reaches the burst
     * limit, one normal-priority launch is allowed through even if a
     * high-priority vNPU is still active within the 5ms window. Reset to 0
     * by the normal-priority launch that consumes the entitlement. */
    uint64_t high_launch_count;
    uint8_t vnpu_priority[MCS_MAX_VNPU];
    uint32_t magic_number;
} mcs_shared_state_t;

/* Shared memory management */
int mcs_shm_init(const char *shm_id, mcs_shared_state_t **out_state);
void mcs_shm_cleanup(const char *shm_id, mcs_shared_state_t *state);

/* Priority scheduling */
void mcs_mark_active(mcs_shared_state_t *state, int vnpu_id);
int mcs_has_high_priority_active(mcs_shared_state_t *state, int my_vnpu_id);
void mcs_wait_for_idle(mcs_shared_state_t *state, int my_vnpu_id);

/* Per-launch hook: high priority increments the burst counter; normal
 * priority waits while a high-priority vNPU is active AND the burst counter
 * is below the limit, then consumes the entitlement by resetting the
 * counter, and marks itself active. */
void mcs_before_launch(mcs_shared_state_t *state, int vnpu_id, uint8_t my_priority);

#if defined(__cplusplus)
}
#endif

#endif /* MCS_SCHED_H */
