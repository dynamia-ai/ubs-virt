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
 * Provides guard_memory() for enforcing memory quota before allocation.
 */

#ifndef __COMMON_MEM_LIMITER_H__
#define __COMMON_MEM_LIMITER_H__

#include <inttypes.h>
#include <stdbool.h>
#include <stddef.h>

#if defined(__cplusplus)
extern "C" {
#endif

/* Memory quota and usage queries - each project (vcann-rt/MCS) must provide these */
extern size_t get_mem_limit_quota(void);
extern int get_mem_used(size_t *used);

/* Optional callback invoked after each successful device allocation.
 * MCS overrides this to track per-process memory usage locally.
 * Defined as weak in mem_limiter.c so projects that don't need it
 * can leave it undefined (linker resolves to the empty weak stub). */
extern void on_mem_alloc(size_t size);

/* Optional callback invoked after each successful device free, with the
 * size recovered from the hook-side allocation registry. Same weak-symbol
 * arrangement as on_mem_alloc. */
extern void on_mem_free(size_t size);

extern int guard_memory(size_t requested);
extern bool memory_check(size_t requested);

#if defined(__cplusplus)
}
#endif

#endif /* __COMMON_MEM_LIMITER_H__ */
