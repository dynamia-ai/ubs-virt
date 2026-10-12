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

#ifndef BPF_DATA_H
#define BPF_DATA_H
#include "vmlinux.h"

#include <bpf/bpf_helpers.h>
#define INTERVAL_NS (1ULL * 1000 * 1000 * 1000)
#define BUF_SIZE (64ULL * 1024)
#define MAX_CPUS (1ULL * 128)
#define MAX_TASKS (1ULL * 1024)

// Buffer for transferring data
struct {
    __uint(type, BPF_MAP_TYPE_RINGBUF);
    __uint(max_entries, BUF_SIZE);
} events SEC(".maps");

// Per-CPU timer for controlling submission frequency without cross-CPU races.
struct {
    __uint(type, BPF_MAP_TYPE_PERCPU_ARRAY);
    __uint(key_size, sizeof(u32));
    __uint(value_size, sizeof(u64));
    __uint(max_entries, 1);
} last_submit_time SEC(".maps");

const u32 first_item = 0;

#endif
