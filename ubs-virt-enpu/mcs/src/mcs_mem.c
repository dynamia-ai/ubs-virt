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
 *
 * MCS memory quota implementation.
 * Provides get_mem_limit_quota() and get_mem_used() for the common
 * memory limiter module, using MCS config for quota and local tracking
 * for device memory usage.
 *
 * IMPORTANT: We do NOT query the device (rtMemGetInfo) during allocation
 * hooks because the NPU driver is not reentrant — calling rtMemGetInfo
 * from within an rtMalloc hook deadlocks at the kernel driver level.
 * Instead, usage is tracked locally: the hooks record each successful
 * allocation and erase it on the matching free (see mem_track_* in
 * common/memory.c), so g_mem_used_local reflects live usage.
 */

#include <stddef.h>
#include <stdio.h>
#include "mcs_config.h"
#include "mem_limiter.h"

#define MB_TO_B (1024UL * 1024UL)

/* Process-local live-usage counter, updated by on_mem_alloc()/on_mem_free().
 * The pointer->size registry that makes frees possible lives in
 * common/memory.c (mem_track_*): the hooks record each successful
 * allocation and erase it again on the matching rtFree/rtDvppFree/rtMemFree,
 * so the quota measures memory actually in use rather than cumulative
 * allocations. */
static size_t g_mem_used_local = 0;

size_t get_mem_limit_quota(void)
{
    /* MCS config stores memory_quota in MB, convert to bytes */
    return (size_t)g_mcs_config.memory_quota * MB_TO_B;
}

int get_mem_used(size_t *used)
{
    if (used == NULL) {
        return -1;
    }
    *used = g_mem_used_local;
    return 0;
}

/* Overrides the weak default in common/mem_limiter.c */
void on_mem_alloc(size_t size)
{
    g_mem_used_local += size;
}

/* Overrides the weak default in common/mem_limiter.c */
void on_mem_free(size_t size)
{
    if (size > g_mem_used_local) {
        g_mem_used_local = 0;
    } else {
        g_mem_used_local -= size;
    }
}
