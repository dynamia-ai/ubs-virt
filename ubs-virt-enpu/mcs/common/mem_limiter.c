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
 * Common memory limiter (former shared module, MCS-private since the 2026-10 reorg).
 * Provides guard_memory() - quota check before device memory allocation.
 * The quota and usage come from get_mem_limit_quota()/get_mem_used(), which
 * each project provides (MCS: process-local live-usage tracking, see
 * mcs_mem.c). The old cross-process file lock was removed: it caused
 * deadlocks when a peer thread or crashed process held it, and the quota
 * check has no need for a shared directory or lock file.
 */

#include "mem_limiter.h"
#include "ascend_compat.h"

/* Weak default — overridden by MCS for per-process tracking */
__attribute__((weak)) void on_mem_alloc(size_t size)
{
    (void)size;
}

/* Weak default — overridden by MCS for per-process tracking */
__attribute__((weak)) void on_mem_free(size_t size)
{
    (void)size;
}

#include <stdio.h>

#define COMMON_ENPU_SUCCESS 0

/* Condition check macros (self-contained) */
#define COMMON_CHECK_COND_RETURN(cond, rt, fmt, ...)             \
    do {                                                         \
        if (cond) {                                              \
            fprintf(stderr, "COMMON: " fmt "\n", ##__VA_ARGS__); \
            return rt;                                           \
        }                                                        \
    } while (0)

bool memory_check(size_t requested)
{
    size_t used = 0;
    int ret = get_mem_used(&used);
    COMMON_CHECK_COND_RETURN(ret != 0, false, "get mem used failed.");
    size_t quota = get_mem_limit_quota();
    size_t total;
    bool result = __builtin_add_overflow(requested, used, &total);
    COMMON_CHECK_COND_RETURN(result, false, "User requested mem size too big! Request:%zu B, used:%zu B, quota:%zu B.",
                             requested, used, quota);
    COMMON_CHECK_COND_RETURN((total > quota), false, "Out of memory! Request:%zu B, used:%zu B, quota:%zu B.",
                             requested, used, quota);
    return true;
}

int guard_memory(size_t requested)
{
    /*
     * Thread-local recursion guard: dlsym(RTLD_NEXT) inside get_mem_used()
     * may trigger a nested rtMalloc -> guard_memory call.  Without this guard
     * we would recurse infinitely.  The file lock is deliberately omitted:
     * it caused deadlocks when a peer thread or crashed process held it, and
     * the device-level query (rtMemGetInfo) is authoritative on its own.
     */
    static __thread int in_guard = 0;
    if (in_guard) {
        return memory_check(requested) ? COMMON_ENPU_SUCCESS : ACL_ERROR_STORAGE_OVER_LIMIT;
    }
    in_guard = 1;

    if (!memory_check(requested)) {
        fprintf(stderr, "COMMON: Guard memory out of memory error for requested:%zd\n", requested);
        in_guard = 0;
        return ACL_ERROR_STORAGE_OVER_LIMIT;
    }
    in_guard = 0;
    return COMMON_ENPU_SUCCESS;
}
