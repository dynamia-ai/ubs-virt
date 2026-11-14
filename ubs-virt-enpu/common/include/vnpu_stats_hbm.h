/* Copyright (c) Huawei Technologies Co., Ltd. 2026. All rights reserved.
 * Licensed under Mulan PSL v2.
 */
#ifndef VNPU_STATS_HBM_H
#define VNPU_STATS_HBM_H

#include <stdbool.h>
#include "vnpu_stats_shm_abi.h"

typedef struct {
    bool consistent;
    uint64_t requested_generation;
    uint64_t generation;
    uint64_t producer_instance;
    uint64_t owner_epoch;
    uint64_t used_bytes;
    uint64_t sampled_at_ns;
    uint64_t attempted_at_ns;
    int32_t source_error;
    uint32_t status;
} vnpu_hbm_snapshot_t;

#ifdef __cplusplus
#define HBM_LOAD(p) ((p)->load())
#define HBM_STORE(p, v) ((p)->store(v))
#else
#define HBM_LOAD(p) atomic_load(p)
#define HBM_STORE(p, v) atomic_store(p, v)
#endif

/* All shared accesses, including payload, are sequentially consistent atomics.
 * One writer must hold the kernel OFD byte-range lock throughout publication.
 * A crashed odd sequence is repaired by its successor under the same lock.
 */
static inline void vnpu_hbm_publish(vnpu_stats_hbm_slot_t *slot, const vnpu_hbm_snapshot_t *value)
{
    uint64_t seq = HBM_LOAD(&slot->sequence);
    seq += seq & 1ULL;
    HBM_STORE(&slot->sequence, seq + 1);
    HBM_STORE(&slot->generation, value->generation);
    HBM_STORE(&slot->producer_instance, value->producer_instance);
    HBM_STORE(&slot->owner_epoch, value->owner_epoch);
    HBM_STORE(&slot->used_bytes, value->used_bytes);
    HBM_STORE(&slot->sampled_at_ns, value->sampled_at_ns);
    HBM_STORE(&slot->attempted_at_ns, value->attempted_at_ns);
    HBM_STORE(&slot->source_error, (uint32_t)value->source_error);
    HBM_STORE(&slot->status, value->status);
    HBM_STORE(&slot->sequence, seq + 2);
}

static inline vnpu_hbm_snapshot_t vnpu_hbm_read(const vnpu_stats_hbm_slot_t *slot)
{
    vnpu_hbm_snapshot_t value = {0};
    value.requested_generation = HBM_LOAD(&slot->requested_generation);
    for (int i = 0; i < VNPU_STATS_HBM_READ_RETRIES; ++i) {
        uint64_t seq = HBM_LOAD(&slot->sequence);
        if ((seq & 1ULL) != 0) {
            continue;
        }
        value.generation = HBM_LOAD(&slot->generation);
        value.requested_generation = HBM_LOAD(&slot->requested_generation);
        value.producer_instance = HBM_LOAD(&slot->producer_instance);
        value.owner_epoch = HBM_LOAD(&slot->owner_epoch);
        value.used_bytes = HBM_LOAD(&slot->used_bytes);
        value.sampled_at_ns = HBM_LOAD(&slot->sampled_at_ns);
        value.attempted_at_ns = HBM_LOAD(&slot->attempted_at_ns);
        value.source_error = (int32_t)HBM_LOAD(&slot->source_error);
        value.status = HBM_LOAD(&slot->status);
        if (seq == HBM_LOAD(&slot->sequence) && value.requested_generation == HBM_LOAD(&slot->requested_generation)) {
            value.consistent = true;
            return value;
        }
    }
    value.consistent = false;
    return value;
}

#undef HBM_LOAD
#undef HBM_STORE
#endif
