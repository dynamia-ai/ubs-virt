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
#ifndef __CORE_LIMITER_H__
#define __CORE_LIMITER_H__

#if defined(__cplusplus)
#include <atomic>
using atomic_int = std::atomic<int>;
#else
#include <stdatomic.h>
#endif

#include <acl/acl.h>
#include <inttypes.h>
#include <pthread.h>
#include <runtime/rt.h>

#if defined(__cplusplus)
extern "C" {
#endif

#define MAGIC_INITIALIZED 0x495A4547U  // IZEG (v3: priority array added)
#define MAGIC_INITIALIZING 0x5A494E47U // ZING
#define MAGIC_UNINITIALIZED 0x0
#define MAX_STREAMS_PER_PROCESS 128
#define MAX_EVENT_PER_PROCESS 65000
#define HUNDRED_PERCENT 100
#define NS_PER_US 1000ULL
#define NS_PER_MS 1000000ULL
#define NS_PER_S 1000000000ULL
#define MAX_STREAK 2

#define VNPU_SCHEULE_PERIOD (100ULL * NS_PER_MS)                 // 100 ms
#define VNPU_NORMAL_FAIRNESS_PERIOD (1ULL * NS_PER_S)            // Reserve normal-priority submit time once per second.
#define VNPU_NORMAL_FAIRNESS_WINDOW (10ULL * NS_PER_MS)          // Normal-priority submit window: 1% admission time.
#define VNPU_FLUSH_PERIOD (1ULL * NS_PER_MS)                     // 1ms
#define VNPU_TIMEOUT_PERIOD (3ULL * NS_PER_MS)                   // 3ms
#define VNPU_NO_TASK_TIMEOUT_PERIOD (5ULL * NS_PER_MS)           // 5ms
#define WAITING_SLEEP_PERIOD (100ULL * NS_PER_US)                // 100 us
#define WATTING_SLIDE_WINDOW_TIMEOUT_PERIOD (100ULL * NS_PER_MS) // 100ms
#define BORROW_TIMESLICE_LENGTH (3ULL * NS_PER_MS)               // 3ms
#define DCMI_TIMEOUT_THRESHOLD (100ULL * NS_PER_MS)              // 100ms
#define UTILIZATION_RATE_MAX (95)
#define UTILIZATION_RATE_MIN (80)
#define SYNC_WAIT_TIMEOUT_NS (200ULL * NS_PER_MS)

typedef struct cache_streams {
    int num_streams;
    rtStream_t streams[MAX_STREAMS_PER_PROCESS];
} cache_streams_t;

typedef void (*core_function)(void *param, rtStream_t stream);

extern pthread_mutex_t g_sched_mutex;
extern atomic_int hasModelExecuteSync;
extern atomic_int waitEventCount;
extern bool get_aclrt_impl_hook_enable(void);
extern void set_aclrt_impl_hook_enable(bool enable);
extern bool get_vnpu_stats_enable(void);
extern void set_vnpu_stats_enable(bool enable);
extern int aicore_limiter_initialize(void);
extern void core_limiter(rtStream_t stream, core_function func, void *param);
extern void set_stream_capture(void *param, rtStream_t stream);
extern void set_event_create_status(void *evt);
extern void set_event_wait_status(void *evt, rtStream_t stm);
extern void set_event_record_status(void *evt, rtStream_t stm);
extern void add_stream(rtStream_t stream);
extern void remove_stream(void *unused, rtStream_t stm);
extern void set_event_destroy_status(void *evt);

extern bool stream_is_capturing(rtStream_t stm);
extern void capture_stats_add(rtStream_t stm, uint32_t block_dim);
extern int capture_stats_transfer_to_model(rtStream_t stm, rtModel_t mdl);
extern int model_stats_get(rtModel_t mdl, uint64_t *block_dim, uint64_t *count);
extern void task_grp_begin(rtStream_t stm);
extern void task_grp_end(rtStream_t stm, rtTaskGrp_t handle);
extern void task_update_begin(rtStream_t stm, rtTaskGrp_t handle);
extern void task_update_end(rtStream_t stm);
extern void launch_stats_dispatch(rtStream_t stm, uint32_t block_dim);

uint64_t ns_now(void);
void update_last_kernel_time_now(void);
void ns_sleep(uint64_t ns);
extern void synchronize_and_clear_streams(void);

#if defined(__cplusplus)
}
#endif

#endif // CORE_LIMITER_H
