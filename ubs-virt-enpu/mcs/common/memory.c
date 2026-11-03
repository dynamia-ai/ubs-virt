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
 * Common memory allocation hooks (former shared module, MCS-private since the 2026-10 reorg).
 * Intercepts rtMalloc, rtMallocCached, etc. to enforce memory quota
 * via guard_memory() before forwarding to the real runtime.
 *
 * IMPORTANT: These hooks must be safe to call during early process
 * startup (before main()).  During that phase we cannot call dlsym,
 * strcmp, or any non-trivial library function.  We use a simple flag
 * (g_mem_hooks_active) that is set by the first rtSetDevice call.
 * Before that flag is set, hooks pass through via dlsym(RTLD_NEXT).
 *
 * Live-usage accounting: free entry points (rtFree, rtDvppFree, rtMemFree)
 * take only a pointer, so the size of each live allocation is recorded in
 * the fixed-size registry below; the matching free erases the entry and
 * reports the size through on_mem_free(). Without this, the quota would
 * be charged on cumulative allocations and long-running alloc/free
 * workloads would exhaust it while actual usage stays low.
 */

#include "ascend_compat.h"
#include "mem_limiter.h"
#include "runtime_hook.h"

#include <dlfcn.h>
#include <pthread.h>

#define COMMON_ENPU_SUCCESS 0

/* Atomic-like flag: 0 = pass-through, 1 = full interception */
static volatile int g_mem_hooks_active = 0;

void activate_mem_hooks(void)
{
    g_mem_hooks_active = 1;
}

/* ---------------------------------------------------------------------------
 * Live-allocation registry: key -> size.
 * Fixed-size open-addressing hash, malloc-free and dlsym-free, so it is safe
 * to touch from inside allocation hooks. Keys are the returned device
 * pointers (always aligned, bit 0 clear); rtMallocPhysical returns an
 * integer handle instead, shifted and tagged with bit 0 to avoid collisions.
 * ------------------------------------------------------------------------- */
#define MEM_TRACK_SLOTS 65536u
#define MEM_TRACK_MASK (MEM_TRACK_SLOTS - 1u)

typedef struct {
    uintptr_t key; /* 0 = empty slot */
    size_t size;
} mem_track_slot_t;

static mem_track_slot_t g_mem_track[MEM_TRACK_SLOTS];
static pthread_mutex_t g_mem_track_lock = PTHREAD_MUTEX_INITIALIZER;

static uint32_t mem_track_hash(uintptr_t key)
{
    return (uint32_t)((key >> 4) * 2654435761u) & MEM_TRACK_MASK;
}

/* Records the allocation and returns the delta to add to the usage counter
 * (size for a new key, size - old_size if the key was already live). Returns
 * SIZE_MAX if the registry is full (the alloc then goes uncounted).
 * The delta scheme keeps accounting correct even if a runtime's internal
 * code routes a second public entry point for the same pointer through the
 * hooks: the counter moves by the real change, never by a duplicated size. */
static size_t mem_track_insert(uintptr_t key, size_t size)
{
    size_t delta = SIZE_MAX;
    (void)pthread_mutex_lock(&g_mem_track_lock);
    uint32_t idx = mem_track_hash(key);
    for (uint32_t probe = 0; probe < MEM_TRACK_SLOTS; probe++) {
        mem_track_slot_t *slot = &g_mem_track[(idx + probe) & MEM_TRACK_MASK];
        if (slot->key == 0) {
            slot->key = key;
            slot->size = size;
            delta = size;
            break;
        }
        if (slot->key == key) {
            /* Same key live twice: should not happen (runtime never hands out
             * a live pointer twice); adjust by the difference and stay
             * consistent instead of double-counting. */
            delta = (size > slot->size) ? size - slot->size : 0;
            slot->size = size;
            break;
        }
    }
    (void)pthread_mutex_unlock(&g_mem_track_lock);
    return delta;
}

/* Erases the entry; returns its size, or 0 if the key was not tracked
 * (untracked frees simply report 0, which on_mem_free treats as a no-op). */
static size_t mem_track_erase(uintptr_t key)
{
    size_t size = 0;
    (void)pthread_mutex_lock(&g_mem_track_lock);
    uint32_t idx = mem_track_hash(key);
    for (uint32_t probe = 0; probe < MEM_TRACK_SLOTS; probe++) {
        mem_track_slot_t *slot = &g_mem_track[(idx + probe) & MEM_TRACK_MASK];
        if (slot->key == 0) {
            break; /* open addressing: end of cluster */
        }
        if (slot->key == key) {
            size = slot->size;
            slot->key = 0;
            slot->size = 0;
            break;
        }
    }
    (void)pthread_mutex_unlock(&g_mem_track_lock);
    return size;
}

/* Resolve real function via dlsym(RTLD_NEXT) — safe before C runtime init? */
/* We avoid this during early startup by checking g_mem_hooks_active first. */

RUNTIME_HOOK_DEFINE(rtMalloc, void **devPtr, uint64_t size, rtMemType_t type, const uint16_t moduleId)
{
    if (!g_mem_hooks_active) {
        /* Pass-through during early startup */
        typedef rtError_t (*real_fn_t)(void **, uint64_t, rtMemType_t, uint16_t);
        static real_fn_t real = NULL;
        if (real == NULL) {
            real = (real_fn_t)dlsym(RTLD_NEXT, "rtMalloc");
        }
        return real ? real(devPtr, size, type, moduleId) : -1;
    }
    int ret = guard_memory(size);
    if (ret != COMMON_ENPU_SUCCESS) {
        return ret;
    }
    typedef rtError_t (*real_fn_t)(void **, uint64_t, rtMemType_t, uint16_t);
    static real_fn_t real = NULL;
    if (real == NULL) {
        real = (real_fn_t)dlsym(RTLD_NEXT, "rtMalloc");
    }
    if (real == NULL) {
        return -1;
    }
    ret = real(devPtr, size, type, moduleId);
    if (ret == COMMON_ENPU_SUCCESS && devPtr != NULL && *devPtr != NULL) {
        size_t delta = mem_track_insert((uintptr_t)*devPtr, (size_t)size);
        if (delta != SIZE_MAX) {
            on_mem_alloc(delta);
        }
    }
    return ret;
}

RUNTIME_HOOK_DEFINE(rtMallocCached, void **devPtr, uint64_t size, rtMemType_t type, const uint16_t moduleId)
{
    if (!g_mem_hooks_active) {
        typedef rtError_t (*real_fn_t)(void **, uint64_t, rtMemType_t, uint16_t);
        static real_fn_t real = NULL;
        if (real == NULL) {
            real = (real_fn_t)dlsym(RTLD_NEXT, "rtMallocCached");
        }
        return real ? real(devPtr, size, type, moduleId) : -1;
    }
    int ret = guard_memory(size);
    if (ret != COMMON_ENPU_SUCCESS) {
        return ret;
    }
    typedef rtError_t (*real_fn_t)(void **, uint64_t, rtMemType_t, uint16_t);
    static real_fn_t real = NULL;
    if (real == NULL) {
        real = (real_fn_t)dlsym(RTLD_NEXT, "rtMallocCached");
    }
    if (real == NULL) {
        return -1;
    }
    ret = real(devPtr, size, type, moduleId);
    if (ret == COMMON_ENPU_SUCCESS && devPtr != NULL && *devPtr != NULL) {
        size_t delta = mem_track_insert((uintptr_t)*devPtr, (size_t)size);
        if (delta != SIZE_MAX) {
            on_mem_alloc(delta);
        }
    }
    return ret;
}

RUNTIME_HOOK_DEFINE(rtDvppMalloc, void **devPtr, uint64_t size, uint16_t moduleId)
{
    if (!g_mem_hooks_active) {
        typedef rtError_t (*real_fn_t)(void **, uint64_t, uint16_t);
        static real_fn_t real = NULL;
        if (real == NULL) {
            real = (real_fn_t)dlsym(RTLD_NEXT, "rtDvppMalloc");
        }
        return real ? real(devPtr, size, moduleId) : -1;
    }
    int ret = guard_memory(size);
    if (ret != COMMON_ENPU_SUCCESS) {
        return ret;
    }
    typedef rtError_t (*real_fn_t)(void **, uint64_t, uint16_t);
    static real_fn_t real = NULL;
    if (real == NULL) {
        real = (real_fn_t)dlsym(RTLD_NEXT, "rtDvppMalloc");
    }
    if (real == NULL) {
        return -1;
    }
    ret = real(devPtr, size, moduleId);
    if (ret == COMMON_ENPU_SUCCESS && devPtr != NULL && *devPtr != NULL) {
        size_t delta = mem_track_insert((uintptr_t)*devPtr, (size_t)size);
        if (delta != SIZE_MAX) {
            on_mem_alloc(delta);
        }
    }
    return ret;
}

RUNTIME_HOOK_DEFINE(rtDvppMallocWithFlag, void **devPtr, uint64_t size, uint32_t flag, const uint16_t moduleId)
{
    if (!g_mem_hooks_active) {
        typedef rtError_t (*real_fn_t)(void **, uint64_t, uint32_t, uint16_t);
        static real_fn_t real = NULL;
        if (real == NULL) {
            real = (real_fn_t)dlsym(RTLD_NEXT, "rtDvppMallocWithFlag");
        }
        return real ? real(devPtr, size, flag, moduleId) : -1;
    }
    int ret = guard_memory(size);
    if (ret != COMMON_ENPU_SUCCESS) {
        return ret;
    }
    typedef rtError_t (*real_fn_t)(void **, uint64_t, uint32_t, uint16_t);
    static real_fn_t real = NULL;
    if (real == NULL) {
        real = (real_fn_t)dlsym(RTLD_NEXT, "rtDvppMallocWithFlag");
    }
    if (real == NULL) {
        return -1;
    }
    ret = real(devPtr, size, flag, moduleId);
    if (ret == COMMON_ENPU_SUCCESS && devPtr != NULL && *devPtr != NULL) {
        size_t delta = mem_track_insert((uintptr_t)*devPtr, (size_t)size);
        if (delta != SIZE_MAX) {
            on_mem_alloc(delta);
        }
    }
    return ret;
}

RUNTIME_HOOK_DEFINE(rtMemAlloc, void **devPtr, uint64_t size, rtMallocPolicy policy, rtMallocAdvise advise,
                    rtMallocConfig_t *cfg)
{
    if (!g_mem_hooks_active) {
        typedef rtError_t (*real_fn_t)(void **, uint64_t, rtMallocPolicy, rtMallocAdvise, rtMallocConfig_t *);
        static real_fn_t real = NULL;
        if (real == NULL) {
            real = (real_fn_t)dlsym(RTLD_NEXT, "rtMemAlloc");
        }
        return real ? real(devPtr, size, policy, advise, cfg) : -1;
    }
    int ret = guard_memory(size);
    if (ret != COMMON_ENPU_SUCCESS) {
        return ret;
    }
    typedef rtError_t (*real_fn_t)(void **, uint64_t, rtMallocPolicy, rtMallocAdvise, rtMallocConfig_t *);
    static real_fn_t real = NULL;
    if (real == NULL) {
        real = (real_fn_t)dlsym(RTLD_NEXT, "rtMemAlloc");
    }
    if (real == NULL) {
        return -1;
    }
    ret = real(devPtr, size, policy, advise, cfg);
    if (ret == COMMON_ENPU_SUCCESS && devPtr != NULL && *devPtr != NULL) {
        size_t delta = mem_track_insert((uintptr_t)*devPtr, (size_t)size);
        if (delta != SIZE_MAX) {
            on_mem_alloc(delta);
        }
    }
    return ret;
}

RUNTIME_HOOK_DEFINE(rtMemAllocManaged, void **ptr, uint64_t size, uint32_t flag, const uint16_t moduleId)
{
    if (!g_mem_hooks_active) {
        typedef rtError_t (*real_fn_t)(void **, uint64_t, uint32_t, uint16_t);
        static real_fn_t real = NULL;
        if (real == NULL) {
            real = (real_fn_t)dlsym(RTLD_NEXT, "rtMemAllocManaged");
        }
        return real ? real(ptr, size, flag, moduleId) : -1;
    }
    int ret = guard_memory(size);
    if (ret != COMMON_ENPU_SUCCESS) {
        return ret;
    }
    typedef rtError_t (*real_fn_t)(void **, uint64_t, uint32_t, uint16_t);
    static real_fn_t real = NULL;
    if (real == NULL) {
        real = (real_fn_t)dlsym(RTLD_NEXT, "rtMemAllocManaged");
    }
    if (real == NULL) {
        return -1;
    }
    ret = real(ptr, size, flag, moduleId);
    if (ret == COMMON_ENPU_SUCCESS && ptr != NULL && *ptr != NULL) {
        size_t delta = mem_track_insert((uintptr_t)*ptr, (size_t)size);
        if (delta != SIZE_MAX) {
            on_mem_alloc(delta);
        }
    }
    return ret;
}

RUNTIME_HOOK_DEFINE(rtMallocPhysical, rtDrvMemHandle *handle, size_t size, rtDrvMemProp_t *prop, uint64_t flags)
{
    if (!g_mem_hooks_active) {
        typedef rtError_t (*real_fn_t)(rtDrvMemHandle *, size_t, rtDrvMemProp_t *, uint64_t);
        static real_fn_t real = NULL;
        if (real == NULL) {
            real = (real_fn_t)dlsym(RTLD_NEXT, "rtMallocPhysical");
        }
        return real ? real(handle, size, prop, flags) : -1;
    }
    int ret = guard_memory(size);
    if (ret != COMMON_ENPU_SUCCESS) {
        return ret;
    }
    typedef rtError_t (*real_fn_t)(rtDrvMemHandle *, size_t, rtDrvMemProp_t *, uint64_t);
    static real_fn_t real = NULL;
    if (real == NULL) {
        real = (real_fn_t)dlsym(RTLD_NEXT, "rtMallocPhysical");
    }
    if (real == NULL) {
        return -1;
    }
    ret = real(handle, size, prop, flags);
    if (ret == COMMON_ENPU_SUCCESS && handle != NULL) {
        size_t delta = mem_track_insert(((uintptr_t)(uint32_t)*handle << 1) | 1u, (size_t)size);
        if (delta != SIZE_MAX) {
            on_mem_alloc(delta);
        }
    }
    return ret;
}

RUNTIME_HOOK_DEFINE(rtFree, void *devPtr)
{
    if (!g_mem_hooks_active) {
        typedef rtError_t (*real_fn_t)(void *);
        static real_fn_t real = NULL;
        if (real == NULL) {
            real = (real_fn_t)dlsym(RTLD_NEXT, "rtFree");
        }
        return real ? real(devPtr) : -1;
    }
    typedef rtError_t (*real_fn_t)(void *);
    static real_fn_t real = NULL;
    if (real == NULL) {
        real = (real_fn_t)dlsym(RTLD_NEXT, "rtFree");
    }
    if (real == NULL) {
        return -1;
    }
    rtError_t ret = real(devPtr);
    if (ret == COMMON_ENPU_SUCCESS && devPtr != NULL) {
        on_mem_free(mem_track_erase((uintptr_t)devPtr));
    }
    return ret;
}

RUNTIME_HOOK_DEFINE(rtDvppFree, void *devPtr)
{
    if (!g_mem_hooks_active) {
        typedef rtError_t (*real_fn_t)(void *);
        static real_fn_t real = NULL;
        if (real == NULL) {
            real = (real_fn_t)dlsym(RTLD_NEXT, "rtDvppFree");
        }
        return real ? real(devPtr) : -1;
    }
    typedef rtError_t (*real_fn_t)(void *);
    static real_fn_t real = NULL;
    if (real == NULL) {
        real = (real_fn_t)dlsym(RTLD_NEXT, "rtDvppFree");
    }
    if (real == NULL) {
        return -1;
    }
    rtError_t ret = real(devPtr);
    if (ret == COMMON_ENPU_SUCCESS && devPtr != NULL) {
        on_mem_free(mem_track_erase((uintptr_t)devPtr));
    }
    return ret;
}

RUNTIME_HOOK_DEFINE(rtMemFree, void *devPtr)
{
    if (!g_mem_hooks_active) {
        typedef rtError_t (*real_fn_t)(void *);
        static real_fn_t real = NULL;
        if (real == NULL) {
            real = (real_fn_t)dlsym(RTLD_NEXT, "rtMemFree");
        }
        return real ? real(devPtr) : -1;
    }
    typedef rtError_t (*real_fn_t)(void *);
    static real_fn_t real = NULL;
    if (real == NULL) {
        real = (real_fn_t)dlsym(RTLD_NEXT, "rtMemFree");
    }
    if (real == NULL) {
        return -1;
    }
    rtError_t ret = real(devPtr);
    if (ret == COMMON_ENPU_SUCCESS && devPtr != NULL) {
        on_mem_free(mem_track_erase((uintptr_t)devPtr));
    }
    return ret;
}

RUNTIME_HOOK_DEFINE(rtMemGetInfoEx, rtMemInfoType_t memInfoType, size_t *freeSize, size_t *totalSize)
{
    if (!g_mem_hooks_active) {
        typedef rtError_t (*real_fn_t)(rtMemInfoType_t, size_t *, size_t *);
        static real_fn_t real = NULL;
        if (real == NULL) {
            real = (real_fn_t)dlsym(RTLD_NEXT, "rtMemGetInfoEx");
        }
        return real ? real(memInfoType, freeSize, totalSize) : -1;
    }
    size_t quota = get_mem_limit_quota();
    size_t used;
    int ret = get_mem_used(&used);
    if (ret != COMMON_ENPU_SUCCESS) {
        return -1;
    }
    *freeSize = (used >= quota) ? 0 : (quota - used);
    *totalSize = quota;
    return COMMON_ENPU_SUCCESS;
}
