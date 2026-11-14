/*
 * Copyright (c) Huawei Technologies Co., Ltd. 2026-2026. All rights reserved.
 * ubs-virt-enpu is licensed under Mulan PSL v2.
 * You can use this software according to the terms and conditions of the Mulan PSL v2.
 * You may obtain a copy of the License at:
 *          http://license.coscl.org.cn/MulanPSL2
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND,
 * EITHER EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT,
 * MERCHANTABILITY OR FIT FOR A PARTICULAR PURPOSE.
 * See the Mulan PSL v2 for more details.
 */

#ifndef __VNPU_STATS_SHM_ABI_H__
#define __VNPU_STATS_SHM_ABI_H__

#include <stdint.h>

#if defined(__cplusplus)
#include <atomic>
typedef std::atomic<uint32_t> vnpu_stats_atomic_u32_t;
typedef std::atomic<uint64_t> vnpu_stats_atomic_u64_t;
#else
#include <stdatomic.h>
typedef _Atomic(uint32_t) vnpu_stats_atomic_u32_t;
typedef _Atomic(uint64_t) vnpu_stats_atomic_u64_t;
#endif

#define VNPU_STATS_NS_PER_MS 1000000ULL
#define VNPU_STATS_NS_PER_US 1000ULL

#define VNPU_STATS_MAX_VNPU 100
#define VNPU_STATS_BUCKET_DURATION_NS (50ULL * VNPU_STATS_NS_PER_MS)
#define VNPU_STATS_NUM_BUCKETS 20 /* Match the nominal 1 s window at 50 ms per bucket. */
#define VNPU_STATS_WINDOW_NS (1000ULL * VNPU_STATS_NS_PER_MS)
#define VNPU_STATS_INIT_TIMEOUT_NS (3ULL * VNPU_STATS_NS_PER_MS)
#define VNPU_STATS_WAIT_SLEEP_NS (100ULL * VNPU_STATS_NS_PER_US)
#define VNPU_STATS_AVG_STALE_NS (3ULL * VNPU_STATS_WINDOW_NS)
#define VNPU_STATS_AVG_SLOTS 64
#define VNPU_STATS_SHM_ID_SUFFIX "_stats"
#define VNPU_STATS_ABI_VERSION 3U
#define VNPU_STATS_HBM_PERIOD_NS (1000ULL * VNPU_STATS_NS_PER_MS)
#define VNPU_STATS_HBM_STALE_NS (3ULL * VNPU_STATS_HBM_PERIOD_NS)
#define VNPU_STATS_HBM_READ_RETRIES 8
#define VNPU_STATS_HBM_EMPTY 0U
#define VNPU_STATS_HBM_OK 1U
#define VNPU_STATS_HBM_ERROR 2U

#define VNPU_STATS_MAGIC_UNINITIALIZED 0x0U
#define VNPU_STATS_MAGIC_INITIALIZING 0x5A494E47U /* "ZING" */
#define VNPU_STATS_MAGIC_INITIALIZED 0x53544133U  /* "STA3" */

typedef struct {
    vnpu_stats_atomic_u64_t bucket_start_ns;
    vnpu_stats_atomic_u64_t sum_block_dim;
    vnpu_stats_atomic_u64_t launch_count;
} vnpu_stats_bucket_t;

typedef struct {
    vnpu_stats_atomic_u64_t sum_ns;
    vnpu_stats_atomic_u64_t count;
    vnpu_stats_atomic_u64_t ts_ns;
} vnpu_stats_avg_slot_t;

typedef struct {
    vnpu_stats_atomic_u64_t requested_generation; /* Fences older allocations before lock takeover. */
    vnpu_stats_atomic_u64_t sequence;
    vnpu_stats_atomic_u64_t generation;
    vnpu_stats_atomic_u64_t producer_instance;
    vnpu_stats_atomic_u64_t owner_epoch;
    vnpu_stats_atomic_u64_t used_bytes;
    vnpu_stats_atomic_u64_t sampled_at_ns;
    vnpu_stats_atomic_u64_t attempted_at_ns;
    vnpu_stats_atomic_u32_t source_error; /* int32_t encoded as uint32_t */
    vnpu_stats_atomic_u32_t status;
} vnpu_stats_hbm_slot_t;

typedef struct {
    vnpu_stats_atomic_u32_t magic_number;
    vnpu_stats_atomic_u32_t version;
    vnpu_stats_atomic_u32_t struct_size;
    vnpu_stats_atomic_u32_t capacity;
    vnpu_stats_bucket_t buckets[VNPU_STATS_MAX_VNPU][VNPU_STATS_NUM_BUCKETS];
    vnpu_stats_atomic_u64_t avg_slot_seq[VNPU_STATS_MAX_VNPU];
    vnpu_stats_avg_slot_t avg_slots[VNPU_STATS_MAX_VNPU][VNPU_STATS_AVG_SLOTS];
    vnpu_stats_hbm_slot_t hbm[VNPU_STATS_MAX_VNPU];
} vnpu_stats_shm_t;

#endif /* __VNPU_STATS_SHM_ABI_H__ */
