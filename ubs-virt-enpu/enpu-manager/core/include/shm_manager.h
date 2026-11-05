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

#define SHM_STATE_VERSION 4 /* bump: 新增 ack_pending_seq（换出 ack 欠账标记），结构布局变化 */

#define MAX_PROCESSES_PER_VNPU 64                       /* 每个 vNPU 最多注册的 vcann-rt 进程数 */
#define PROCESS_ALIVE_TIMEOUT_NS (5ULL * 1000000000ULL) /* 进程心跳超时阈值: 5 秒 */

typedef struct process_entry {
    int32_t vcann_rt_id;                    /* vcann-rt 唯一 ID (atomic 计数器分配) */
    int32_t pid;                            /* OS 进程 PID */
    atomic_bool swapped;                    /* 该进程是否已换出 */
    atomic_bool active;                     /* 该进程是否活跃 (false=空槽位) */
    atomic_uint_fast64_t last_heartbeat_ns; /* 心跳时间戳 (进程存活检测) */
    atomic_uint_fast64_t hbm_used;          /* 该进程实际占用的 HBM 字节数 */
    uint64_t swap_offset;                   /* 该进程在 swap buffer 中的偏移 */
    uint64_t swap_size;                     /* 该进程换出的数据大小 */
    /* 换出 ack 欠账标记: 0=本槽位不欠 ack, ==swap_cmd.cmd_seq=欠当前 epoch 一次 ack.
     * 写命令时由 enpu-manager 置位, 只有真正 ack(进程自己或 watchdog 代 ack) 才清零.
     * 完成判定 = 目标 vNPU 上没有任何槽位的 ack_pending_seq 等于当前 cmd_seq. */
    atomic_uint_fast64_t ack_pending_seq;
} process_entry_t;

typedef struct swap_cmd {
    char target_pod_uid[MAX_UUID_LEN];
    int32_t target_vnpu_id;
    atomic_int action;
    atomic_bool completed;
    atomic_int refcount;          /* 待换出的进程数，最后一个进程置 completed=true */
    atomic_uint_fast64_t cmd_seq; /* 命令序号（epoch），进程据此避免重复处理/递减 */
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
    atomic_uint_fast64_t hbm_used; /* 该 vNPU 所有进程的 hbm_used 之和 (atomic_fetch_add/sub) */
    int32_t sched_policy;
    bool swap_enabled;
    int32_t swap_priority;
    process_entry_t processes[MAX_PROCESSES_PER_VNPU]; /* 进程注册表（嵌套） */
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
    atomic_int next_vcann_rt_id; /* vcann_rt_id 自增计数器（0 表示未分配） */
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
/* watchdog 代死进程 ack: 仅当该槽位 ack_pending_seq==当前 cmd_seq 时清零并递减 refcount,
 * 随后若无任何槽位仍欠本 epoch 的 ack 则置 completed. 幂等, 可重复调用. */
int shm_manager_proxy_ack_process(shm_manager_t *mgr, int phy_id, int vnpu_id, int slot);
/* 兜底完成判定: 当前换出命令仍待完成且目标 vNPU 上无槽位欠 ack 时置 completed.
 * 用于覆盖"进程 ack 后半路消失/槽位被复用"等无法靠逐进程代 ack 收敛的残留场景. */
int shm_manager_try_complete_swap_cmd(shm_manager_t *mgr, int phy_id);

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
/* 进程级 swap buffer 分配/释放：偏移与大小记录在 processes[slot] */
int shm_alloc_per_process(shm_state_t *state, int vnpu_id, int slot, uint64_t size);
int shm_free_per_process(shm_state_t *state, int vnpu_id, int slot);
/* 聚合查询: 该 vNPU 是否"存在活跃进程且全部已换出"（空 vNPU 返回 false） */
int shm_manager_get_vnpu_all_swapped(shm_manager_t *mgr, int phy_id, int vnpu_id, bool *swapped);
void shm_print_free_block_info(void);

#if defined(__cplusplus)
}
#endif

#endif
