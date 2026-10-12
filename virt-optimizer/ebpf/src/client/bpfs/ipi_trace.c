/*
 * Copyright (c) Huawei Technologies Co., Ltd. 2025-2025. All rights reserved.
 *
 * ubs-optimizer is licensed under Mulan PSL v2.
 * You can use this software according to the terms and conditions of the Mulan PSL v2.
 * You may obtain a copy of Mulan PSL v2 at:
 * http://license.coscl.org.cn/MulanPSL2
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND,
 * EITHER EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT,
 * MERCHANTABILITY OR FIT FOR A PARTICULAR PURPOSE.
 * See the Mulan PSL v2 for more details.
 */

#include "bpf_include.h"

// Collected data
struct system_event_data {
    u64 ipi_count;
    u64 transmission_delay_ns;
    u64 processing_delay_ns;
};

// Counter for ipi_count
struct {
    __uint(type, BPF_MAP_TYPE_PERCPU_ARRAY);
    __uint(key_size, sizeof(u32));
    __uint(value_size, sizeof(u64));
    __uint(max_entries, 1);
} counter SEC(".maps");

// Summation of transmission_delay_ns
struct {
    __uint(type, BPF_MAP_TYPE_PERCPU_ARRAY);
    __uint(key_size, sizeof(u32));
    __uint(value_size, sizeof(u64));
    __uint(max_entries, 1);
} transmission_delay_ns_map SEC(".maps");

// Summation of processing_delay_ns
struct {
    __uint(type, BPF_MAP_TYPE_PERCPU_ARRAY);
    __uint(key_size, sizeof(u32));
    __uint(value_size, sizeof(u64));
    __uint(max_entries, 1);
} processing_delay_ns_map SEC(".maps");

// Timestamp of last ipi_raise
struct {
    __uint(type, BPF_MAP_TYPE_HASH);
    __uint(key_size, sizeof(u32));
    __uint(value_size, sizeof(u64));
    __uint(max_entries, 1);
} ipi_raise_map SEC(".maps");

// Timestamp of last ipi_entry
struct {
    __uint(type, BPF_MAP_TYPE_PERCPU_ARRAY);
    __uint(key_size, sizeof(u32));
    __uint(value_size, sizeof(u64));
    __uint(max_entries, 1);
} ipi_entry_map SEC(".maps");

#if defined(__TARGET_ARCH_arm64)
SEC("tracepoint/ipi/ipi_raise")
int handle_ipi_raise()
{
    u64 now = bpf_ktime_get_ns();
    bpf_map_update_elem(&ipi_raise_map, &first_item, &now, BPF_ANY);
    return 0;
}
#endif

#if defined(__TARGET_ARCH_arm64)
SEC("tracepoint/ipi/ipi_entry")
#elif defined(__TARGET_ARCH_x86_64)
SEC("tracepoint/irq_vectors/call_function_entry")
#else
undefined
#endif
int handle_ipi_entry()
{
    u64 now = bpf_ktime_get_ns();
    u64 *ipi_entry_timestamp = bpf_map_lookup_elem(&ipi_entry_map, &first_item);
    if (ipi_entry_timestamp) {
        *ipi_entry_timestamp = now;
    }

#if defined(__aarch64__)
    u64 *ipi_raise_timestamp = bpf_map_lookup_elem(&ipi_raise_map, &first_item);
    if (!ipi_raise_timestamp) {
        return 0;
    }
    u64 *transmission_delay_accumulator = bpf_map_lookup_elem(&transmission_delay_ns_map, &first_item);
    if (transmission_delay_accumulator) {
        *transmission_delay_accumulator += now - (*ipi_raise_timestamp);
    }
#endif

    u64 *last_time = bpf_map_lookup_elem(&last_submit_time, &first_item);
    if (!last_time) {
        return 0;
    }
    if (*last_time == 0) {
        *last_time = now;
        return 0;
    }
    if (now - *last_time <= INTERVAL_NS) {
        return 0;
    }

    struct system_event_data *event = bpf_ringbuf_reserve(&events, sizeof(*event), 0);
    if (!event) {
        return 0;
    }

    u64 *count = bpf_map_lookup_elem(&counter, &first_item);
    u64 *transmission_delay_ns = bpf_map_lookup_elem(&transmission_delay_ns_map, &first_item);
    u64 *processing_delay_ns = bpf_map_lookup_elem(&processing_delay_ns_map, &first_item);
    event->ipi_count = count ? *count : 0;
    event->transmission_delay_ns = transmission_delay_ns ? *transmission_delay_ns : 0;
    event->processing_delay_ns = processing_delay_ns ? *processing_delay_ns : 0;
    if (count) {
        *count = 0;
    }
    if (transmission_delay_ns) {
        *transmission_delay_ns = 0;
    }
    if (processing_delay_ns) {
        *processing_delay_ns = 0;
    }
    *last_time = now;
    bpf_ringbuf_submit(event, 0);
    return 0;
}

#if defined(__TARGET_ARCH_arm64)
SEC("tracepoint/ipi/ipi_exit")
#elif defined(__TARGET_ARCH_x86_64)
SEC("tracepoint/irq_vectors/call_function_exit")
#endif
int handle_ipi_exit()
{
    u64 now = bpf_ktime_get_ns();

    u64 *ipi_entry_timestamp = bpf_map_lookup_elem(&ipi_entry_map, &first_item);
    if (!ipi_entry_timestamp || *ipi_entry_timestamp == 0) {
        return 0;
    }

    u64 *count = bpf_map_lookup_elem(&counter, &first_item);
    u64 *processing_delay_ns = bpf_map_lookup_elem(&processing_delay_ns_map, &first_item);
    if (count) {
        (*count)++;
    }
    if (processing_delay_ns) {
        *processing_delay_ns += now - (*ipi_entry_timestamp);
    }
    return 0;
}

char _license[] SEC("license") = "GPL";
