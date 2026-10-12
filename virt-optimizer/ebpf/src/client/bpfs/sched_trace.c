/*
 * Copyright (c) Huawei Technologies Co., Ltd. 2025-2025. All rights reserved.
 *
 * ubs-optimizer is licensed under Mulan PSL v2.
 * You can use this software according to the terms and conditions of the Mulan PSL v2.
 * You may obtain a copy of Mulan PSL v2 at:
 *          http://license.coscl.org.cn/MulanPSL2
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND,
 * EITHER EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT,
 * MERCHANTABILITY OR FIT FOR A PARTICULAR PURPOSE.
 * See the Mulan PSL v2 for more details.
 */

#include "bpf_include.h"

// Collected data
struct system_event_data {
    u64 sched_switch_count;
    u64 sched_migrate_count;
};

// Counter for sched_switch_count
struct {
    __uint(type, BPF_MAP_TYPE_PERCPU_ARRAY);
    __uint(key_size, sizeof(u32));
    __uint(value_size, sizeof(u64));
    __uint(max_entries, 1);
} sched_switch_counter SEC(".maps");

// Counter for sched_migrate_count
struct {
    __uint(type, BPF_MAP_TYPE_PERCPU_ARRAY);
    __uint(key_size, sizeof(u32));
    __uint(value_size, sizeof(u64));
    __uint(max_entries, 1);
} sched_migrate_counter SEC(".maps");

static __always_inline void submit_event_if_due(u64 now)
{
    u64 *last_time = bpf_map_lookup_elem(&last_submit_time, &first_item);
    if (!last_time) {
        return;
    }
    if (*last_time == 0) {
        *last_time = now;
        return;
    }
    if (now - *last_time <= INTERVAL_NS) {
        return;
    }

    struct system_event_data *event = bpf_ringbuf_reserve(&events, sizeof(*event), 0);
    if (!event) {
        return;
    }
    u64 *switch_count = bpf_map_lookup_elem(&sched_switch_counter, &first_item);
    u64 *migrate_count = bpf_map_lookup_elem(&sched_migrate_counter, &first_item);
    event->sched_switch_count = switch_count ? *switch_count : 0;
    event->sched_migrate_count = migrate_count ? *migrate_count : 0;
    if (switch_count) {
        *switch_count = 0;
    }
    if (migrate_count) {
        *migrate_count = 0;
    }
    *last_time = now;
    bpf_ringbuf_submit(event, 0);
}

SEC("tracepoint/sched/sched_switch")
int handle_sched_switch()
{
    u64 *count = bpf_map_lookup_elem(&sched_switch_counter, &first_item);
    if (count) {
        (*count)++;
    }
    submit_event_if_due(bpf_ktime_get_ns());
    return 0;
}

SEC("tracepoint/sched/sched_migrate_task")
int handle_sched_migrate()
{
    u64 *migrate_count = bpf_map_lookup_elem(&sched_migrate_counter, &first_item);
    if (migrate_count) {
        (*migrate_count)++;
    }
    submit_event_if_due(bpf_ktime_get_ns());
    return 0;
}

char _license[] SEC("license") = "GPL";
