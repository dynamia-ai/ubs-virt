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
#ifndef __SHM_MANAGER_H__
#define __SHM_MANAGER_H__

#include "common.h"

#if defined(__cplusplus)
#include <atomic>
using atomic_uint_fast64_t = std::atomic<uint_fast64_t>;
using atomic_bool = std::atomic<bool>;
using atomic_int = std::atomic<int>;
#else
#include <stdatomic.h>
#endif

#define SHM_STATE_VERSION 4 /* bump: 新增 ack_pending_seq（换出 ack 欠账标记），结构布局变化 */
#define SWAP_ACTION_NONE 0
#define SWAP_ACTION_OUT 1
#define SWAP_ACTION_IN 2
#define SWAP_OUT_NONE 0
#define SWAP_OUT_FROM_BORROWED 1
#define SWAP_OUT_FROM_ALL 2

#define MAX_UUID_LEN 64
#define DIE_ID_LEN 64
#define MAX_VNPU_PER_DIE 128
#define MAX_PROCESSES_PER_VNPU 64 /* 每个 vNPU 最多注册的 vcann-rt 进程数 */
#define MB_TO_B (1024 * 1024)

#if defined(__cplusplus)
extern "C" {
#endif

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

shm_state_t *shm_state_posix_shm_attach(int phy_id, const char *die_id);
void shm_state_posix_shm_detach(shm_state_t *state);

vnpu_shm_entry_t *shm_get_entry(shm_state_t *state, int32_t vnpu_id);
int shm_update_used(shm_state_t *state, int32_t vnpu_id, uint64_t used);
uint64_t shm_get_dynamic_free(shm_state_t *state);
uint64_t shm_get_hbm_request_free(shm_state_t *state);
int shm_update_hbm_request(shm_state_t *state, int32_t vnpu_id, uint64_t request_quota);

/* 进程注册表：vcann-rt 启动时注册，退出时注销（实现见 npu_manager.c，写入 g_npu_info） */
int register_to_shm_state(shm_state_t *state, int vnpu_id);
void unregister_from_shm_state(shm_state_t *state, int vnpu_id, int slot);

/* 进程级 swapped 操作 */
void shm_set_process_swapped(shm_state_t *state, int32_t vnpu_id, int slot, bool swapped);
bool shm_get_process_swapped(shm_state_t *state, int32_t vnpu_id, int slot);
/* 聚合查询：该 vNPU 的所有活跃进程是否都已换出（无活跃进程视为已换出；参数非法返回 false） */
bool shm_get_vnpu_all_swapped(shm_state_t *state, int32_t vnpu_id);

int shm_check_swap_cmd(shm_state_t *state, int32_t *vnpu_id, int32_t *action);
void shm_ack_swap_cmd(shm_state_t *state);

/* 判断目标 vNPU 上是否还有槽位欠 seq 这次 ack: true=全部已 ack(可以置 completed) */
bool shm_swap_ack_done(shm_state_t *state, int32_t vnpu_id, uint64_t seq);
/* 以本进程身份 ack 当前 epoch: atomic_exchange 抢占保证恰好一次.
 * 返回 >=0: ack 成功, 值为递减后的 refcount; 返回 -1: 本槽位不欠本 epoch(已 ack/被代 ack/参数非法) */
int shm_try_ack_swap_cmd(shm_state_t *state, int32_t vnpu_id, int slot, uint64_t seq);

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

#if defined(__cplusplus)
}
#endif

#endif