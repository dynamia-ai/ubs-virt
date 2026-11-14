/* Copyright (c) Huawei Technologies Co., Ltd. 2026. All rights reserved.
 * Licensed under Mulan PSL v2.
 */
#ifndef ENPU_VNPU_SCHED_SHM_ABI_H
#define ENPU_VNPU_SCHED_SHM_ABI_H
#include <pthread.h>
#include <stdint.h>
#ifdef __cplusplus
#include <atomic>
using atomic_int = std::atomic<int>;
using atomic_uint_fast8_t = std::atomic<uint_fast8_t>;
using atomic_uint_fast32_t = std::atomic<uint_fast32_t>;
using atomic_uint_fast64_t = std::atomic<uint_fast64_t>;
#else
#include <stdatomic.h>
#endif

/* Preserve the current vcann-rt layout and magic. Both endpoints must use this ABI. */
#define VNPU_SCHED_MAX_VNPU 100
#define VNPU_SCHED_MAGIC_INITIALIZED 0x495A4547U /* v3: priority array added */
#define VNPU_SCHED_MAGIC_INITIALIZING 0x5A494E47U
#define VNPU_SCHED_MAGIC_UNINITIALIZED 0U

typedef struct shared_memory {
    pthread_mutex_t vnpu_schedule_mutex[VNPU_SCHED_MAX_VNPU];
    atomic_uint_fast64_t last_alive_time_ns[VNPU_SCHED_MAX_VNPU];
    atomic_uint_fast64_t last_kernel_time_ns[VNPU_SCHED_MAX_VNPU];
    atomic_uint_fast8_t vnpu_schedule_turn[VNPU_SCHED_MAX_VNPU];
    atomic_uint_fast8_t vnpu_core_limit_quota[VNPU_SCHED_MAX_VNPU];
    atomic_uint_fast8_t vnpu_priority[VNPU_SCHED_MAX_VNPU];
    atomic_int owner;
    atomic_uint_fast32_t magic_number;
    atomic_int slide_window_len;
    atomic_uint_fast64_t last_slide_window_time_ns;
    pthread_mutex_t npu_utilization_monitor_mutex;
    atomic_uint_fast64_t vnpu_quota_timeslice[VNPU_SCHED_MAX_VNPU];
    atomic_uint_fast64_t vnpu_cur_timeslice[VNPU_SCHED_MAX_VNPU];
    atomic_uint_fast8_t vnpu_quota_injected_for_turn[VNPU_SCHED_MAX_VNPU];
    atomic_int vnpu_sync_expected[VNPU_SCHED_MAX_VNPU];
    atomic_int vnpu_sync_completed[VNPU_SCHED_MAX_VNPU];
    atomic_uint_fast64_t vnpu_turn_max_sync[VNPU_SCHED_MAX_VNPU];
    atomic_uint_fast64_t vnpu_timeslice_for_turn[VNPU_SCHED_MAX_VNPU];
} vnpu_time_slice_sched_t;
#endif
