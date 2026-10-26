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

#ifndef __SHM_MANAGER_H__
#define __SHM_MANAGER_H__

#include <pthread.h>
#include <stdatomic.h>
#include <stdbool.h>
#include "common.h"
#include "enpu_manager.h"
#include "log.h"

#define SHM_ID_FORMAT "/die-%d-%d"

#define MAGIC_UNINITIALIZED 0x0U
#define MAGIC_INITIALIZING 0x5A494E47U
#define MAGIC_INITIALIZED 0x495A4547U

#define MAX_VNPU_SCHED 100

#define SWAP_ACTION_NONE 0
#define SWAP_ACTION_OUT 1
#define SWAP_ACTION_IN 2
#define SWAP_OUT_NONE 0
#define SWAP_OUT_FROM_BORROWED 1
#define SWAP_OUT_FROM_ALL 2

#define SHM_STATE_VERSION 2 /* bump: 删除 pod_alive 字段，结构布局变化 */

typedef struct swap_cmd {
    char target_pod_uid[MAX_UUID_LEN];
    int32_t target_vnpu_id;
    atomic_int action;
    atomic_bool completed;
    uint64_t timestamp_ns;
} swap_cmd_t;

typedef struct swap_out_cmd {
    int32_t target_vnpu_id;
    atomic_int action;
    atomic_bool completed;
} swap_out_cmd_t;

typedef struct vnpu_time_slice_sched {
    pthread_mutex_t vnpu_schedule_mutex[MAX_VNPU_SCHED];
    atomic_uint_fast64_t last_alive_time_ns[MAX_VNPU_SCHED];
    atomic_uint_fast64_t last_kernel_time_ns[MAX_VNPU_SCHED];
    atomic_uint_fast8_t vnpu_schedule_turn[MAX_VNPU_SCHED];
    atomic_uint_fast8_t vnpu_core_limit_quota[MAX_VNPU_SCHED];
    atomic_uint_fast8_t vnpu_priority[MAX_VNPU_SCHED];
    atomic_int owner;
    atomic_uint_fast32_t magic_number;
    atomic_int slide_window_len;
    atomic_uint_fast64_t last_slide_window_time_ns;
    pthread_mutex_t npu_utilization_monitor_mutex;
    atomic_uint_fast64_t vnpu_quota_timeslice[MAX_VNPU_SCHED];
    atomic_uint_fast64_t vnpu_cur_timeslice[MAX_VNPU_SCHED];
    atomic_uint_fast8_t vnpu_quota_injected_for_turn[MAX_VNPU_SCHED];
    atomic_int vnpu_sync_expected[MAX_VNPU_SCHED];
    atomic_int vnpu_sync_completed[MAX_VNPU_SCHED];
    atomic_uint_fast64_t vnpu_turn_max_sync[MAX_VNPU_SCHED];
    atomic_uint_fast64_t vnpu_timeslice_for_turn[MAX_VNPU_SCHED];
} vnpu_time_slice_sched_t;

typedef struct vnpu_shm_entry {
    int32_t vnpu_id;
    uint64_t hbm_request;
    uint64_t hbm_limit;
    atomic_uint_fast64_t hbm_used;
    int32_t sched_policy;
    bool swapped;
    uint64_t swap_offset;
    uint64_t swap_size;
    bool swap_enabled;
    int32_t swap_priority;
} vnpu_shm_entry_t;

typedef struct shm_state {
    uint32_t version;
    int32_t phy_id;
    char shm_id[DIE_ID_LEN];
    uint64_t hbm_total;
    vnpu_shm_entry_t entries[MAX_VNPU_PER_DIE];
    atomic_uint_fast64_t vnpu_bitmap[2];
    int32_t sched_policy;
    atomic_bool initialized;
    void *swap_buffer_base;
    swap_cmd_t swap_cmd;
    swap_out_cmd_t swap_out_cmd;
    pthread_mutex_t swap_in_mutex;
    uint8_t reserved[64];
} shm_state_t;

typedef struct shm_manager {
    pthread_mutex_t lock;
    shm_state_t *shm_states[MAX_NPU_PER_NODE];
    vnpu_time_slice_sched_t *sched_structs[MAX_NPU_PER_NODE];
    int shm_state_count;
    bool initialized;
} shm_manager_t;

static inline bool vnpu_bitmap_test(const atomic_uint_fast64_t bitmap[2], int vnpu_id)
{
    if (vnpu_id < 0 || vnpu_id >= MAX_VNPU_PER_DIE)
        return false;
    int idx = vnpu_id / 64;
    int bit = vnpu_id % 64;
    return (atomic_load(&bitmap[idx]) >> bit) & 1;
}

static inline void vnpu_bitmap_set(atomic_uint_fast64_t bitmap[2], int vnpu_id)
{
    if (vnpu_id < 0 || vnpu_id >= MAX_VNPU_PER_DIE)
        return;
    int idx = vnpu_id / 64;
    int bit = vnpu_id % 64;
    atomic_fetch_or(&bitmap[idx], (1ULL << bit));
}

static inline void vnpu_bitmap_clear(atomic_uint_fast64_t bitmap[2], int vnpu_id)
{
    if (vnpu_id < 0 || vnpu_id >= MAX_VNPU_PER_DIE)
        return;
    int idx = vnpu_id / 64;
    int bit = vnpu_id % 64;
    atomic_fetch_and(&bitmap[idx], ~(1ULL << bit));
}

#if defined(__cplusplus)
extern "C" {
#endif

shm_manager_t *shm_manager_create(void);
int shm_manager_destroy(shm_manager_t *mgr);

int shm_id_format_standardize(int phy_id, const char *die_id_str, char *buf, int buf_len);

int shm_struct_init(const char *shm_id, vnpu_time_slice_sched_t **sched_out);
int shm_struct_cleanup(const char *shm_id, vnpu_time_slice_sched_t *sched, int vnpu_count);

int shm_manager_get_or_create_shm(shm_manager_t *mgr, int phy_id, const char *die_id, char *shm_id_out,
                                  int shm_id_out_len);

int shm_manager_register_vnpu(shm_manager_t *mgr, int phy_id, int vnpu_id, int aicore_quota, int sched_policy);

int shm_manager_unregister_vnpu(shm_manager_t *mgr, int phy_id, int vnpu_id);

shm_state_t *shm_manager_get_state(shm_manager_t *mgr, int phy_id);

int shm_manager_add_entry(shm_manager_t *mgr, int phy_id, vnpu_shm_entry_t *entry);
int shm_manager_remove_entry(shm_manager_t *mgr, int phy_id, int vnpu_id);
int shm_manager_init_state(shm_manager_t *mgr, int phy_id, uint64_t hbm_total);

int shm_manager_set_swap_state(shm_manager_t *mgr, int phy_id, int vnpu_id, uint64_t offset, uint64_t size);
int shm_manager_get_swap_state(shm_manager_t *mgr, int phy_id, int vnpu_id, bool *swapped, uint64_t *offset,
                               uint64_t *size);

int shm_manager_write_swap_cmd(shm_manager_t *mgr, int phy_id, const char *pod_uid, int vnpu_id, int action,
                               uint8_t flag);
int shm_manager_read_swap_cmd(shm_manager_t *mgr, int phy_id, char *pod_uid, int *vnpu_id, int *action,
                              bool *completed);
int shm_manager_ack_swap_cmd(shm_manager_t *mgr, int phy_id);

int shm_manager_set_swap_enabled(shm_manager_t *mgr, int phy_id, int vnpu_id, bool enabled);
int shm_manager_get_swap_enabled(shm_manager_t *mgr, int phy_id, int vnpu_id, bool *enabled);

vnpu_time_slice_sched_t *shm_manager_get_sched_struct(shm_manager_t *mgr, int phy_id);

shm_state_t *shm_state_posix_shm_create(int phy_id, const char *die_id);
shm_state_t *shm_state_posix_shm_attach(int phy_id, const char *die_id);
int shm_state_posix_shm_destroy(int phy_id, const char *die_id);
int shm_manager_get_vnpu_swapped(shm_manager_t *mgr, int phy_id, int vnpu_id, bool *swapped);

swap_buffer_manager_t *shm_swap_buffer_init(uint64_t buffer_size);
int shm_swap_buffer_deinit(swap_buffer_manager_t *mgr);
uint64_t shm_swap_buffer_alloc_offset(shm_state_t *state, uint64_t size);
void shm_swap_buffer_free_offset(shm_state_t *state, uint64_t offset, uint64_t size);
int shm_swap_buffer_get_status(swap_buffer_status_t *status);

int shm_alloc(shm_state_t *state, int vnpu_id, uint64_t size);
int shm_free(shm_state_t *state, int vnpu_id);
void shm_print_free_block_info(void);

#if defined(__cplusplus)
}
#endif

#endif
