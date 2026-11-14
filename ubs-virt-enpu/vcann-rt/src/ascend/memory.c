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

#include "core_limiter.h"
#include "log.h"
#include "mem_limiter.h"
#include "npu_manager.h"
#include "runtime_hook.h"
#include "swap_hook.h"

void load_rt_libraries(void);

RUNTIME_HOOK_DEFINE(rtMalloc, void **devPtr, uint64_t size, rtMemType_t type, const uint16_t moduleId)
{
    LOG_DEBUG("Hook mem rtMalloc size:%" PRIu64 ".", size);
    swap_hook_t *swap_hook = swap_hook_get_global();
    int ret = guard_memory(size, swap_hook != NULL);
    if (ret != ENPU_SUCCESS) {
        return ret;
    }
    if (swap_hook == NULL) {
        return RUNTIME_HOOK_CALL(rt_library_entry, rtMalloc, devPtr, size, type, moduleId);
    }

    ret = swap_hook_check_and_swap_in(swap_hook);
    CHECK_RETURN_ERROR_CODE(ret, "Swap hook check and swap in failed when rtMalloc.");
    ret = swap_hook_malloc_mem(swap_hook, devPtr, size);
    CHECK_RETURN_ERROR_CODE(ret, "Swap hook malloc failed.");

    return ACL_RT_SUCCESS;
}

RUNTIME_HOOK_DEFINE(aclrtMallocImpl, void **devPtr, size_t size, aclrtMemMallocPolicy policy)
{
    if (!get_aclrt_impl_hook_enable()) {
        return RUNTIME_HOOK_CALL(rt_library_entry, aclrtMallocImpl, devPtr, size, policy);
    }
    LOG_DEBUG("Hook mem aclrtMallocImpl size:%" PRIu64 ".", size);
    int ret = guard_memory(size, swap_hook_get_global() != NULL);
    if (ret != ENPU_SUCCESS) {
        return ret;
    }
    return RUNTIME_HOOK_CALL(rt_library_entry, aclrtMallocImpl, devPtr, size, policy);
}

RUNTIME_HOOK_DEFINE(aclrtMallocAlign32Impl, void **devPtr, size_t size, aclrtMemMallocPolicy policy)
{
    if (!get_aclrt_impl_hook_enable()) {
        return RUNTIME_HOOK_CALL(rt_library_entry, aclrtMallocAlign32Impl, devPtr, size, policy);
    }
    LOG_DEBUG("Hook mem aclrtMallocAlign32Impl size:%" PRIu64 ".", size);
    int ret = guard_memory(size, swap_hook_get_global() != NULL);
    if (ret != ENPU_SUCCESS) {
        return ret;
    }
    return RUNTIME_HOOK_CALL(rt_library_entry, aclrtMallocAlign32Impl, devPtr, size, policy);
}

RUNTIME_HOOK_DEFINE(rtMallocCached, void **devPtr, uint64_t size, rtMemType_t type, const uint16_t moduleId)
{
    LOG_DEBUG("Hook mem rtMallocCached size:%" PRIu64 ".", size);
    int ret = guard_memory(size, swap_hook_get_global() != NULL);
    if (ret != ENPU_SUCCESS) {
        return ret;
    }
    return RUNTIME_HOOK_CALL(rt_library_entry, rtMallocCached, devPtr, size, type, moduleId);
}

RUNTIME_HOOK_DEFINE(aclrtMallocCachedImpl, void **devPtr, size_t size, aclrtMemMallocPolicy policy)
{
    if (!get_aclrt_impl_hook_enable()) {
        return RUNTIME_HOOK_CALL(rt_library_entry, aclrtMallocCachedImpl, devPtr, size, policy);
    }
    LOG_DEBUG("Hook mem aclrtMallocCachedImpl size:%" PRIu64 ".", size);
    int ret = guard_memory(size, swap_hook_get_global() != NULL);
    if (ret != ENPU_SUCCESS) {
        return ret;
    }
    return RUNTIME_HOOK_CALL(rt_library_entry, aclrtMallocCachedImpl, devPtr, size, policy);
}

RUNTIME_HOOK_DEFINE(rtDvppMalloc, void **devPtr, uint64_t size, uint16_t moduleId)
{
    LOG_DEBUG("Hook mem rtDvppMalloc size:%" PRIu64 ".", size);
    int ret = guard_memory(size, swap_hook_get_global() != NULL);
    if (ret != ENPU_SUCCESS) {
        return ret;
    }
    return RUNTIME_HOOK_CALL(rt_library_entry, rtDvppMalloc, devPtr, size, moduleId);
}

RUNTIME_HOOK_DEFINE(rtDvppMallocWithFlag, void **devPtr, uint64_t size, uint32_t flag, const uint16_t moduleId)
{
    LOG_DEBUG("Hook mem rtDvppMallocWithFlag size:%" PRIu64 ".", size);
    int ret = guard_memory(size, swap_hook_get_global() != NULL);
    if (ret != ENPU_SUCCESS) {
        return ret;
    }
    return RUNTIME_HOOK_CALL(rt_library_entry, rtDvppMallocWithFlag, devPtr, size, flag, moduleId);
}

RUNTIME_HOOK_DEFINE(rtMemAlloc, void **devPtr, uint64_t size, rtMallocPolicy policy, rtMallocAdvise advise,
                    rtMallocConfig_t *cfg)
{
    LOG_DEBUG("Hook mem rtMemAlloc size:%" PRIu64 ".", size);
    int ret = guard_memory(size, swap_hook_get_global() != NULL);
    if (ret != ENPU_SUCCESS) {
        return ret;
    }
    return RUNTIME_HOOK_CALL(rt_library_entry, rtMemAlloc, devPtr, size, policy, advise, cfg);
}

RUNTIME_HOOK_DEFINE(rtMemAllocManaged, void **ptr, uint64_t size, uint32_t flag, const uint16_t moduleId)
{
    LOG_DEBUG("Hook mem rtMemAllocManaged size:%" PRIu64 ".", size);
    int ret = guard_memory(size, swap_hook_get_global() != NULL);
    if (ret != ENPU_SUCCESS) {
        return ret;
    }
    return RUNTIME_HOOK_CALL(rt_library_entry, rtMemAllocManaged, ptr, size, flag, moduleId);
}

RUNTIME_HOOK_DEFINE(aclrtMemAllocManagedImpl, void **ptr, uint64_t size, uint32_t flag)
{
    if (!get_aclrt_impl_hook_enable()) {
        return RUNTIME_HOOK_CALL(rt_library_entry, aclrtMemAllocManagedImpl, ptr, size, flag);
    }
    LOG_DEBUG("Hook mem aclrtMemAllocManagedImpl size:%" PRIu64 ".", size);
    int ret = guard_memory(size, swap_hook_get_global() != NULL);
    if (ret != ENPU_SUCCESS) {
        return ret;
    }
    return RUNTIME_HOOK_CALL(rt_library_entry, aclrtMemAllocManagedImpl, ptr, size, flag);
}

RUNTIME_HOOK_DEFINE(rtMallocPhysical, rtDrvMemHandle *handle, size_t size, rtDrvMemProp_t *prop, uint64_t flags)
{
    LOG_DEBUG("Hook mem rtMallocPhysical size:%zd.", size);
    if (prop == NULL || prop->side != ACL_MEM_LOCATION_TYPE_DEVICE) {
        return RUNTIME_HOOK_CALL(rt_library_entry, rtMallocPhysical, handle, size, prop, flags);
    }

    swap_hook_t *swap_hook = swap_hook_get_global();
    int ret = guard_memory(size, swap_hook != NULL);
    if (ret != ENPU_SUCCESS) {
        return ret;
    }
    if (swap_hook == NULL) {
        return RUNTIME_HOOK_CALL(rt_library_entry, rtMallocPhysical, handle, size, prop, flags);
    }

    ret = swap_hook_check_and_swap_in(swap_hook);
    CHECK_RETURN_ERROR_CODE(ret, "Swap hook check and swap in failed when rtMallocPhysical.");
    ret = RUNTIME_HOOK_CALL(rt_library_entry, rtMallocPhysical, handle, size, prop, flags);
    if (ret != ACL_RT_SUCCESS) {
        return ret;
    }
    ret = swap_hook_malloc_physical_mem(swap_hook, *handle, size, prop, flags);
    CHECK_RETURN_ERROR_CODE(ret, "Swap hook malloc physical failed.");

    return ACL_RT_SUCCESS;
}

RUNTIME_HOOK_DEFINE(aclrtMallocPhysicalImpl, aclrtDrvMemHandle *handle, size_t size, const aclrtPhysicalMemProp *prop,
                    uint64_t flags)
{
    if (!get_aclrt_impl_hook_enable()) {
        return RUNTIME_HOOK_CALL(rt_library_entry, aclrtMallocPhysicalImpl, handle, size, prop, flags);
    }
    LOG_DEBUG("Hook mem aclrtMallocPhysicalImpl size:%zd.", size);
    int ret = guard_memory(size, swap_hook_get_global() != NULL);
    if (ret != ENPU_SUCCESS) {
        return ret;
    }
    return RUNTIME_HOOK_CALL(rt_library_entry, aclrtMallocPhysicalImpl, handle, size, prop, flags);
}

RUNTIME_HOOK_DEFINE(rtMemGetInfoEx, rtMemInfoType_t memInfoType, size_t *freeSize, size_t *totalSize)
{
    (void)memInfoType;
    int res = log_init();
    CHECK_COND_RETURN_((res != ENPU_SUCCESS), res, "Failed to init log module, res:%d.", res);
    pre_rt_init();
    enpu_global_init();
    CHECK_COND_RETURN_(!check_init_success(), ACL_ERROR_UNINITIALIZE,
                       "Failed to initialize vcann-rt, please check the config file in %s.", NPU_CONFIG_PATH);
    LOG_DEBUG("Hook mem rtMemGetInfoEx.");
    if (freeSize == NULL || totalSize == NULL) {
        LOG_ERROR("rtMemGetInfoEx: freeSize or totalSize is NULL.");
        return RT_ERROR_INVALID_VALUE;
    }
    size_t quota = get_mem_limit_quota();
    size_t used;
    int ret = get_mem_used(&used);
    if (ret != 0) {
        LOG_ERROR("Get mem used failed.");
        return RT_ERROR_INVALID_VALUE;
    }
    if (used > quota) {
        LOG_ERROR("Mem used is abnormally high, exceeding the quota.");
        return RT_ERROR_INVALID_VALUE;
    }
    size_t remain = quota - used;
    if (get_shm_state() == NULL || quota == get_mem_request_quota()) {
        *freeSize = remain;
        *totalSize = quota;
        return RT_ERROR_NONE;
    }

    size_t dynamic_free = get_mem_dynamic_free();
    size_t request = get_mem_request_quota();
    if (request > used) {
        size_t free_mem_1 = request - used + dynamic_free;
        size_t free_mem_2 = (free_mem_1 < remain) ? free_mem_1 : remain;
        *freeSize = (free_mem_2 > request - used) ? free_mem_2 : request - used;
    } else {
        *freeSize = (dynamic_free < remain) ? dynamic_free : remain;
    }
    *totalSize = quota;
    return RT_ERROR_NONE;
}

RUNTIME_HOOK_DEFINE(aclrtGetMemInfoImpl, aclrtMemAttr attr, size_t *free, size_t *total)
{
    if (!get_aclrt_impl_hook_enable()) {
        return RUNTIME_HOOK_CALL(rt_library_entry, aclrtGetMemInfoImpl, attr, free, total);
    }
    (void)attr;
    int res = log_init();
    CHECK_COND_RETURN_((res != ENPU_SUCCESS), res, "Failed to init log module, res:%d.", res);
    pre_rt_init();
    enpu_global_init();
    CHECK_COND_RETURN_(!check_init_success(), ACL_ERROR_UNINITIALIZE,
                       "Failed to initialize vcann-rt, please check the config file in %s.", NPU_CONFIG_PATH);
    LOG_DEBUG("Hook mem aclrtGetMemInfoImpl.");
    if (free == NULL || total == NULL) {
        LOG_ERROR("aclrtGetMemInfoImpl: free or total is NULL.");
        return RT_ERROR_INVALID_VALUE;
    }
    size_t quota = get_mem_limit_quota();
    size_t used;
    int ret = get_mem_used(&used);
    if (ret != 0) {
        LOG_ERROR("Get mem used failed.");
        return RT_ERROR_INVALID_VALUE;
    }
    if (used > quota) {
        LOG_ERROR("Mem used is abnormally high, exceeding the quota.");
        return RT_ERROR_INVALID_VALUE;
    }
    size_t remain = quota - used;
    *free = remain;
    *total = quota;
    return ACL_RT_SUCCESS;
}
/* mem-swap 扩展: 显存换入换出所需内存接口拦截, 初始化序列与上方 master 版 hook 保持一致 */
RUNTIME_HOOK_DEFINE(rtFree, void *devPtr)
{
    int res = log_init();
    CHECK_COND_RETURN_((res != ENPU_SUCCESS), res, "Failed to init log module, res:%d.", res);
    pre_rt_init();
    enpu_global_init();
    CHECK_COND_RETURN_(!check_init_success(), ACL_ERROR_UNINITIALIZE,
                       "Failed to initialize vcann-rt, please check the config file in %s.", NPU_CONFIG_PATH);

    if (get_shm_state() == NULL) {
        return RUNTIME_HOOK_CALL(rt_library_entry, rtFree, devPtr);
    }

    swap_hook_t *swap_hook = swap_hook_get_global();
    if (swap_hook == NULL) {
        return RUNTIME_HOOK_CALL(rt_library_entry, rtFree, devPtr);
    }
    int ret = swap_hook_check_and_swap_in(swap_hook);
    CHECK_RETURN_ERROR_CODE(ret, "Swap hook check and swap in failed when rtFree.");
    ret = swap_hook_free_mem(swap_hook, devPtr);
    CHECK_RETURN_ERROR_CODE(ret, "Swap hook free mem failed when rtFree.");

    return ACL_RT_SUCCESS;
}

RUNTIME_HOOK_DEFINE(rtFreePhysical, rtDrvMemHandle handle)
{
    int res = log_init();
    CHECK_COND_RETURN_((res != ENPU_SUCCESS), res, "Failed to init log module, res:%d.", res);
    pre_rt_init();
    enpu_global_init();
    CHECK_COND_RETURN_(!check_init_success(), ACL_ERROR_UNINITIALIZE,
                       "Failed to initialize vcann-rt, please check the config file in %s.", NPU_CONFIG_PATH);

    if (get_shm_state() == NULL) {
        return RUNTIME_HOOK_CALL(rt_library_entry, rtFreePhysical, handle);
    }

    swap_hook_t *swap_hook = swap_hook_get_global();
    if (swap_hook == NULL) {
        return RUNTIME_HOOK_CALL(rt_library_entry, rtFreePhysical, handle);
    }
    int ret = swap_hook_check_and_swap_in(swap_hook);
    CHECK_RETURN_ERROR_CODE(ret, "Swap hook check and swap in failed when rtFreePhysical.");
    ret = swap_hook_free_physical_mem(swap_hook, handle);
    CHECK_RETURN_ERROR_CODE(ret, "Swap hook free mem failed when rtFreePhysical.");

    return ACL_RT_SUCCESS;
}

RUNTIME_HOOK_DEFINE(rtMapMem, void *devPtr, size_t size, size_t offset, rtDrvMemHandle handle, uint64_t flags)
{
    int res = log_init();
    CHECK_COND_RETURN_((res != ENPU_SUCCESS), res, "Failed to init log module, res:%d.", res);
    pre_rt_init();
    enpu_global_init();
    CHECK_COND_RETURN_(!check_init_success(), ACL_ERROR_UNINITIALIZE,
                       "Failed to initialize vcann-rt, please check the config file in %s.", NPU_CONFIG_PATH);

    if (get_shm_state() == NULL) {
        return RUNTIME_HOOK_CALL(rt_library_entry, rtMapMem, devPtr, size, offset, handle, flags);
    }

    swap_hook_t *swap_hook = swap_hook_get_global();
    if (swap_hook == NULL) {
        return RUNTIME_HOOK_CALL(rt_library_entry, rtMapMem, devPtr, size, offset, handle, flags);
    }
    int ret = swap_hook_check_and_swap_in(swap_hook);
    CHECK_RETURN_ERROR_CODE(ret, "Swap hook check and swap in failed when rtMapMem.");
    ret = swap_hook_map_mem(swap_hook, devPtr, size, offset, &handle, flags);
    CHECK_RETURN_ERROR_CODE(ret, "Swap hook map mem failed when rtMapMem.");
    return RUNTIME_HOOK_CALL(rt_library_entry, rtMapMem, devPtr, size, offset, handle, flags);
}

RUNTIME_HOOK_DEFINE(rtUnmapMem, void *devPtr)
{
    int res = log_init();
    CHECK_COND_RETURN_((res != ENPU_SUCCESS), res, "Failed to init log module, res:%d.", res);
    pre_rt_init();
    enpu_global_init();
    CHECK_COND_RETURN_(!check_init_success(), ACL_ERROR_UNINITIALIZE,
                       "Failed to initialize vcann-rt, please check the config file in %s.", NPU_CONFIG_PATH);

    if (get_shm_state() == NULL) {
        return RUNTIME_HOOK_CALL(rt_library_entry, rtUnmapMem, devPtr);
    }

    swap_hook_t *swap_hook = swap_hook_get_global();
    if (swap_hook == NULL) {
        return RUNTIME_HOOK_CALL(rt_library_entry, rtUnmapMem, devPtr);
    }
    int ret = swap_hook_check_and_swap_in(swap_hook);
    CHECK_RETURN_ERROR_CODE(ret, "Swap hook check and swap in failed when rtUnmapMem.");
    ret = swap_hook_unmap_mem(swap_hook, devPtr);
    CHECK_RETURN_ERROR_CODE(ret, "Swap hook unmap mem failed when rtUnmapMem.");
    return RUNTIME_HOOK_CALL(rt_library_entry, rtUnmapMem, devPtr);
}

RUNTIME_HOOK_DEFINE(rtMemcpy, void *dst, uint64_t destMax, const void *src, uint64_t cnt, rtMemcpyKind_t kind)
{
    int res = log_init();
    CHECK_COND_RETURN_((res != ENPU_SUCCESS), res, "Failed to init log module, res:%d.", res);
    pre_rt_init();
    enpu_global_init();
    CHECK_COND_RETURN_(!check_init_success(), ACL_ERROR_UNINITIALIZE,
                       "Failed to initialize vcann-rt, please check the config file in %s.", NPU_CONFIG_PATH);

    if (get_shm_state() != NULL) {
        swap_hook_t *swap_hook = swap_hook_get_global();
        int ret = swap_hook_check_and_swap_in(swap_hook);
        CHECK_RETURN_ERROR_CODE(ret, "Swap hook check and swap in failed when rtMemcpy.");
    }

    return RUNTIME_HOOK_CALL(rt_library_entry, rtMemcpy, dst, destMax, src, cnt, kind);
}

RUNTIME_HOOK_DEFINE(rtsPointerGetAttributes, const void *ptr, rtPtrAttributes_t *attributes)
{
    int res = log_init();
    CHECK_COND_RETURN_((res != ENPU_SUCCESS), res, "Failed to init log module, res:%d.", res);
    pre_rt_init();
    enpu_global_init();
    CHECK_COND_RETURN_(!check_init_success(), ACL_ERROR_UNINITIALIZE,
                       "Failed to initialize vcann-rt, please check the config file in %s.", NPU_CONFIG_PATH);

    if (get_shm_state() == NULL) {
        return RUNTIME_HOOK_CALL(rt_library_entry, rtsPointerGetAttributes, ptr, attributes);
    }

    swap_hook_t *swap_hook = swap_hook_get_global();
    int ret = swap_hook_check_and_swap_in(swap_hook);
    CHECK_RETURN_ERROR_CODE(ret, "Swap hook check failed when rtsPointerGetAttributes.");

    return RUNTIME_HOOK_CALL(rt_library_entry, rtsPointerGetAttributes, ptr, attributes);
}

RUNTIME_HOOK_DEFINE(rtMemcpyAsync, void *dst, uint64_t destMax, const void *src, uint64_t cnt, rtMemcpyKind_t kind,
                    rtStream_t stm)
{
    int res = log_init();
    CHECK_COND_RETURN_((res != ENPU_SUCCESS), res, "Failed to init log module, res:%d.", res);
    pre_rt_init();
    enpu_global_init();
    CHECK_COND_RETURN_(!check_init_success(), ACL_ERROR_UNINITIALIZE,
                       "Failed to initialize vcann-rt, please check the config file in %s.", NPU_CONFIG_PATH);

    if (get_shm_state() != NULL) {
        swap_hook_t *swap_hook = swap_hook_get_global();
        int ret = swap_hook_check_and_swap_in(swap_hook);
        CHECK_RETURN_ERROR_CODE(ret, "Swap hook check failed when rtMemcpyAsync.");
    }

    return RUNTIME_HOOK_CALL(rt_library_entry, rtMemcpyAsync, dst, destMax, src, cnt, kind, stm);
}

RUNTIME_HOOK_DEFINE(rtsCheckMemType, void **addrs, uint32_t size, uint32_t memType, uint32_t *checkResult,
                    uint32_t reserve)
{
    int res = log_init();
    CHECK_COND_RETURN_((res != ENPU_SUCCESS), res, "Failed to init log module, res:%d.", res);
    pre_rt_init();
    enpu_global_init();
    CHECK_COND_RETURN_(!check_init_success(), ACL_ERROR_UNINITIALIZE,
                       "Failed to initialize vcann-rt, please check the config file in %s.", NPU_CONFIG_PATH);

    if (get_shm_state() != NULL) {
        swap_hook_t *swap_hook = swap_hook_get_global();
        int ret = swap_hook_check_and_swap_in(swap_hook);
        CHECK_RETURN_ERROR_CODE(ret, "Swap hook check failed when rtsCheckMemType.");
    }

    return RUNTIME_HOOK_CALL(rt_library_entry, rtsCheckMemType, addrs, size, memType, checkResult, reserve);
}

RUNTIME_HOOK_DEFINE(rtMemcpyAsyncEx, void *dst, uint64_t destMax, const void *src, uint64_t cnt, rtMemcpyKind_t kind,
                    rtStream_t stm, rtMemcpyConfig_t *memcpyConfig)
{
    int res = log_init();
    CHECK_COND_RETURN_((res != ENPU_SUCCESS), res, "Failed to init log module, res:%d.", res);
    pre_rt_init();
    enpu_global_init();
    CHECK_COND_RETURN_(!check_init_success(), ACL_ERROR_UNINITIALIZE,
                       "Failed to initialize vcann-rt, please check the config file in %s.", NPU_CONFIG_PATH);

    if (get_shm_state() != NULL) {
        swap_hook_t *swap_hook = swap_hook_get_global();
        int ret = swap_hook_check_and_swap_in(swap_hook);
        CHECK_RETURN_ERROR_CODE(ret, "Swap hook check failed when rtMemcpyAsyncEx.");
    }

    return RUNTIME_HOOK_CALL(rt_library_entry, rtMemcpyAsyncEx, dst, destMax, src, cnt, kind, stm, memcpyConfig);
}

RUNTIME_HOOK_DEFINE(rtsMemcpyBatch, void **dsts, void **srcs, size_t *sizes, size_t count, rtMemcpyBatchAttr *attrs,
                    size_t *attrsIdxs, size_t numAttrs, size_t *failIdx)
{
    int res = log_init();
    CHECK_COND_RETURN_((res != ENPU_SUCCESS), res, "Failed to init log module, res:%d.", res);
    pre_rt_init();
    enpu_global_init();
    CHECK_COND_RETURN_(!check_init_success(), ACL_ERROR_UNINITIALIZE,
                       "Failed to initialize vcann-rt, please check the config file in %s.", NPU_CONFIG_PATH);

    if (get_shm_state() != NULL) {
        swap_hook_t *swap_hook = swap_hook_get_global();
        int ret = swap_hook_check_and_swap_in(swap_hook);
        CHECK_RETURN_ERROR_CODE(ret, "Swap hook check failed when rtsMemcpyBatch.");
    }

    return RUNTIME_HOOK_CALL(rt_library_entry, rtsMemcpyBatch, dsts, srcs, sizes, count, attrs, attrsIdxs, numAttrs,
                             failIdx);
}

RUNTIME_HOOK_DEFINE(rtsMemcpyBatchAsync, void **dsts, size_t *destMaxs, void **srcs, size_t *sizes, size_t count,
                    rtMemcpyBatchAttr *attrs, size_t *attrsIdxs, size_t numAttrs, size_t *failIdx, rtStream_t stream)
{
    int res = log_init();
    CHECK_COND_RETURN_((res != ENPU_SUCCESS), res, "Failed to init log module, res:%d.", res);
    pre_rt_init();
    enpu_global_init();
    CHECK_COND_RETURN_(!check_init_success(), ACL_ERROR_UNINITIALIZE,
                       "Failed to initialize vcann-rt, please check the config file in %s.", NPU_CONFIG_PATH);

    if (get_shm_state() != NULL) {
        swap_hook_t *swap_hook = swap_hook_get_global();
        int ret = swap_hook_check_and_swap_in(swap_hook);
        CHECK_RETURN_ERROR_CODE(ret, "Swap hook check failed when rtsMemcpyBatchAsync.");
    }

    return RUNTIME_HOOK_CALL(rt_library_entry, rtsMemcpyBatchAsync, dsts, destMaxs, srcs, sizes, count, attrs,
                             attrsIdxs, numAttrs, failIdx, stream);
}

RUNTIME_HOOK_DEFINE(rtMemcpy2d, void *dst, uint64_t dstPitch, const void *src, uint64_t srcPitch, uint64_t width,
                    uint64_t height, rtMemcpyKind_t kind)
{
    int res = log_init();
    CHECK_COND_RETURN_((res != ENPU_SUCCESS), res, "Failed to init log module, res:%d.", res);
    pre_rt_init();
    enpu_global_init();
    CHECK_COND_RETURN_(!check_init_success(), ACL_ERROR_UNINITIALIZE,
                       "Failed to initialize vcann-rt, please check the config file in %s.", NPU_CONFIG_PATH);

    if (get_shm_state() != NULL) {
        swap_hook_t *swap_hook = swap_hook_get_global();
        int ret = swap_hook_check_and_swap_in(swap_hook);
        CHECK_RETURN_ERROR_CODE(ret, "Swap hook check failed when rtMemcpy2d.");
    }

    return RUNTIME_HOOK_CALL(rt_library_entry, rtMemcpy2d, dst, dstPitch, src, srcPitch, width, height, kind);
}

RUNTIME_HOOK_DEFINE(rtMemcpy2dAsync, void *dst, uint64_t dstPitch, const void *src, uint64_t srcPitch, uint64_t width,
                    uint64_t height, rtMemcpyKind_t kind, rtStream_t stm)
{
    int res = log_init();
    CHECK_COND_RETURN_((res != ENPU_SUCCESS), res, "Failed to init log module, res:%d.", res);
    pre_rt_init();
    enpu_global_init();
    CHECK_COND_RETURN_(!check_init_success(), ACL_ERROR_UNINITIALIZE,
                       "Failed to initialize vcann-rt, please check the config file in %s.", NPU_CONFIG_PATH);

    if (get_shm_state() != NULL) {
        swap_hook_t *swap_hook = swap_hook_get_global();
        int ret = swap_hook_check_and_swap_in(swap_hook);
        CHECK_RETURN_ERROR_CODE(ret, "Swap hook check failed when rtMemcpy2dAsync.");
    }

    return RUNTIME_HOOK_CALL(rt_library_entry, rtMemcpy2dAsync, dst, dstPitch, src, srcPitch, width, height, kind, stm);
}

RUNTIME_HOOK_DEFINE(rtsSetMemcpyDesc, rtMemcpyDesc_t desc, rtMemcpyKind kind, void *srcAddr, void *dstAddr,
                    size_t count, rtMemcpyConfig_t *config)
{
    int res = log_init();
    CHECK_COND_RETURN_((res != ENPU_SUCCESS), res, "Failed to init log module, res:%d.", res);
    pre_rt_init();
    enpu_global_init();
    CHECK_COND_RETURN_(!check_init_success(), ACL_ERROR_UNINITIALIZE,
                       "Failed to initialize vcann-rt, please check the config file in %s.", NPU_CONFIG_PATH);

    if (get_shm_state() != NULL) {
        swap_hook_t *swap_hook = swap_hook_get_global();
        int ret = swap_hook_check_and_swap_in(swap_hook);
        CHECK_RETURN_ERROR_CODE(ret, "Swap hook check failed when rtsSetMemcpyDesc.");
    }

    return RUNTIME_HOOK_CALL(rt_library_entry, rtsSetMemcpyDesc, desc, kind, srcAddr, dstAddr, count, config);
}

RUNTIME_HOOK_DEFINE(rtsMemcpyAsyncWithDesc, rtMemcpyDesc_t desc, rtMemcpyKind kind, rtMemcpyConfig_t *config,
                    rtStream_t stream)
{
    int res = log_init();
    CHECK_COND_RETURN_((res != ENPU_SUCCESS), res, "Failed to init log module, res:%d.", res);
    pre_rt_init();
    enpu_global_init();
    CHECK_COND_RETURN_(!check_init_success(), ACL_ERROR_UNINITIALIZE,
                       "Failed to initialize vcann-rt, please check the config file in %s.", NPU_CONFIG_PATH);

    if (get_shm_state() != NULL) {
        swap_hook_t *swap_hook = swap_hook_get_global();
        int ret = swap_hook_check_and_swap_in(swap_hook);
        CHECK_RETURN_ERROR_CODE(ret, "Swap hook check failed when rtsMemcpyAsyncWithDesc.");
    }

    return RUNTIME_HOOK_CALL(rt_library_entry, rtsMemcpyAsyncWithDesc, desc, kind, config, stream);
}

RUNTIME_HOOK_DEFINE(rtMemcpyAsyncWithOffset, void **dst, uint64_t dstMax, uint64_t dstDataOffset, const void **src,
                    uint64_t cnt, uint64_t srcDataOffset, rtMemcpyKind kind, rtStream_t stm)
{
    int res = log_init();
    CHECK_COND_RETURN_((res != ENPU_SUCCESS), res, "Failed to init log module, res:%d.", res);
    pre_rt_init();
    enpu_global_init();
    CHECK_COND_RETURN_(!check_init_success(), ACL_ERROR_UNINITIALIZE,
                       "Failed to initialize vcann-rt, please check the config file in %s.", NPU_CONFIG_PATH);

    if (get_shm_state() != NULL) {
        swap_hook_t *swap_hook = swap_hook_get_global();
        int ret = swap_hook_check_and_swap_in(swap_hook);
        CHECK_RETURN_ERROR_CODE(ret, "Swap hook check failed when rtMemcpyAsyncWithOffset.");
    }

    return RUNTIME_HOOK_CALL(rt_library_entry, rtMemcpyAsyncWithOffset, dst, dstMax, dstDataOffset, src, cnt,
                             srcDataOffset, kind, stm);
}

RUNTIME_HOOK_DEFINE(rtMemset, void *devPtr, uint64_t destMax, uint32_t val, uint64_t cnt)
{
    int res = log_init();
    CHECK_COND_RETURN_((res != ENPU_SUCCESS), res, "Failed to init log module, res:%d.", res);
    pre_rt_init();
    enpu_global_init();
    CHECK_COND_RETURN_(!check_init_success(), ACL_ERROR_UNINITIALIZE,
                       "Failed to initialize vcann-rt, please check the config file in %s.", NPU_CONFIG_PATH);

    if (get_shm_state() != NULL) {
        swap_hook_t *swap_hook = swap_hook_get_global();
        int ret = swap_hook_check_and_swap_in(swap_hook);
        CHECK_RETURN_ERROR_CODE(ret, "Swap hook check failed when rtMemset.");
    }

    return RUNTIME_HOOK_CALL(rt_library_entry, rtMemset, devPtr, destMax, val, cnt);
}

RUNTIME_HOOK_DEFINE(rtMemsetAsync, void *ptr, uint64_t destMax, uint32_t val, uint64_t cnt, rtStream_t stm)
{
    int res = log_init();
    CHECK_COND_RETURN_((res != ENPU_SUCCESS), res, "Failed to init log module, res:%d.", res);
    pre_rt_init();
    enpu_global_init();
    CHECK_COND_RETURN_(!check_init_success(), ACL_ERROR_UNINITIALIZE,
                       "Failed to initialize vcann-rt, please check the config file in %s.", NPU_CONFIG_PATH);

    if (get_shm_state() != NULL) {
        swap_hook_t *swap_hook = swap_hook_get_global();
        int ret = swap_hook_check_and_swap_in(swap_hook);
        CHECK_RETURN_ERROR_CODE(ret, "Swap hook check failed when rtMemsetAsync.");
    }

    return RUNTIME_HOOK_CALL(rt_library_entry, rtMemsetAsync, ptr, destMax, val, cnt, stm);
}

RUNTIME_HOOK_DEFINE(rtMemPrefetchToDevice, void *devPtr, uint64_t len, int32_t devId)
{
    int res = log_init();
    CHECK_COND_RETURN_((res != ENPU_SUCCESS), res, "Failed to init log module, res:%d.", res);
    pre_rt_init();
    enpu_global_init();
    CHECK_COND_RETURN_(!check_init_success(), ACL_ERROR_UNINITIALIZE,
                       "Failed to initialize vcann-rt, please check the config file in %s.", NPU_CONFIG_PATH);

    if (get_shm_state() != NULL) {
        swap_hook_t *swap_hook = swap_hook_get_global();
        int ret = swap_hook_check_and_swap_in(swap_hook);
        CHECK_RETURN_ERROR_CODE(ret, "Swap hook check failed when rtMemPrefetchToDevice.");
    }

    return RUNTIME_HOOK_CALL(rt_library_entry, rtMemPrefetchToDevice, devPtr, len, devId);
}

RUNTIME_HOOK_DEFINE(rtsIpcMemGetExportKey, const void *ptr, size_t size, char_t *key, uint32_t len, uint64_t flags)
{
    int res = log_init();
    CHECK_COND_RETURN_((res != ENPU_SUCCESS), res, "Failed to init log module, res:%d.", res);
    pre_rt_init();
    enpu_global_init();
    CHECK_COND_RETURN_(!check_init_success(), ACL_ERROR_UNINITIALIZE,
                       "Failed to initialize vcann-rt, please check the config file in %s.", NPU_CONFIG_PATH);

    if (get_shm_state() != NULL) {
        swap_hook_t *swap_hook = swap_hook_get_global();
        int ret = swap_hook_check_and_swap_in(swap_hook);
        CHECK_RETURN_ERROR_CODE(ret, "Swap hook check failed when rtsIpcMemGetExportKey.");
    }

    return RUNTIME_HOOK_CALL(rt_library_entry, rtsIpcMemGetExportKey, ptr, size, key, len, flags);
}

RUNTIME_HOOK_DEFINE(rtsIpcMemImportByKey, void **ptr, const char_t *key, uint64_t flags)
{
    int res = log_init();
    CHECK_COND_RETURN_((res != ENPU_SUCCESS), res, "Failed to init log module, res:%d.", res);
    pre_rt_init();
    enpu_global_init();
    CHECK_COND_RETURN_(!check_init_success(), ACL_ERROR_UNINITIALIZE,
                       "Failed to initialize vcann-rt, please check the config file in %s.", NPU_CONFIG_PATH);

    if (get_shm_state() != NULL) {
        swap_hook_t *swap_hook = swap_hook_get_global();
        int ret = swap_hook_check_and_swap_in(swap_hook);
        CHECK_RETURN_ERROR_CODE(ret, "Swap hook check failed when rtsIpcMemImportByKey.");
    }

    return RUNTIME_HOOK_CALL(rt_library_entry, rtsIpcMemImportByKey, ptr, key, flags);
}

RUNTIME_HOOK_DEFINE(rtsValueWrite, const void *const devAddr, const uint64_t value, const uint32_t flag, rtStream_t stm)
{
    int res = log_init();
    CHECK_COND_RETURN_((res != ENPU_SUCCESS), res, "Failed to init log module, res:%d.", res);
    pre_rt_init();
    enpu_global_init();
    CHECK_COND_RETURN_(!check_init_success(), ACL_ERROR_UNINITIALIZE,
                       "Failed to initialize vcann-rt, please check the config file in %s.", NPU_CONFIG_PATH);

    if (get_shm_state() != NULL) {
        swap_hook_t *swap_hook = swap_hook_get_global();
        int ret = swap_hook_check_and_swap_in(swap_hook);
        CHECK_RETURN_ERROR_CODE(ret, "Swap hook check failed when rtsValueWrite.");
    }

    return RUNTIME_HOOK_CALL(rt_library_entry, rtsValueWrite, devAddr, value, flag, stm);
}

RUNTIME_HOOK_DEFINE(rtsValueWait, const void *const devAddr, const uint64_t value, const uint32_t flag, rtStream_t stm)
{
    int res = log_init();
    CHECK_COND_RETURN_((res != ENPU_SUCCESS), res, "Failed to init log module, res:%d.", res);
    pre_rt_init();
    enpu_global_init();
    CHECK_COND_RETURN_(!check_init_success(), ACL_ERROR_UNINITIALIZE,
                       "Failed to initialize vcann-rt, please check the config file in %s.", NPU_CONFIG_PATH);

    if (get_shm_state() != NULL) {
        swap_hook_t *swap_hook = swap_hook_get_global();
        int ret = swap_hook_check_and_swap_in(swap_hook);
        CHECK_RETURN_ERROR_CODE(ret, "Swap hook check failed when rtsValueWait.");
    }

    return RUNTIME_HOOK_CALL(rt_library_entry, rtsValueWait, devAddr, value, flag, stm);
}
