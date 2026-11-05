/*
 * Copyright (c) Huawei Technologies Co., Ltd. 2026-2026. All rights reserved.
 * ubs-virt-enpu is licensed under Mulan PSL v2.
 */

#include "swap_hook.h"
#include <stdlib.h>
#include <string.h>
#include "core_limiter.h"
#include "log.h"
#include "npu_manager.h"
#include "swap_buffer_shm.h"
#include "swap_monitor_thread.h"

extern void mock_shm_set_swap_state(bool swapped, uint64_t offset, uint64_t size);

static swap_hook_t *g_global_swap_hook = NULL;
static void *g_swap_buffer_base = NULL;

swap_hook_t *swap_hook_create(int vnpu_id, void *executor, void *tracker)
{
    if (executor == NULL || tracker == NULL) {
        LOG_ERROR("swap_hook_create failed: executor=%p, tracker=%p", executor, tracker);
        return NULL;
    }

    if (vnpu_id < 0) {
        LOG_ERROR("swap_hook_create failed: invalid vnpu_id=%d", vnpu_id);
        return NULL;
    }

    swap_hook_t *hook = (swap_hook_t *)calloc(1, sizeof(swap_hook_t));
    if (hook == NULL) {
        LOG_ERROR("swap_hook_create failed: calloc returned NULL");
        return NULL;
    }

    hook->vnpu_id = vnpu_id;
    hook->executor = (swap_executor_t *)executor;
    hook->tracker = (memory_tracker_t *)tracker;

    return hook;
}

int swap_hook_destroy(swap_hook_t *hook)
{
    if (hook == NULL) {
        return ENPU_FAIL;
    }

    free(hook);
    return ENPU_SUCCESS;
}

int swap_hook_malloc_mem(swap_hook_t *hook, void **ptr, uint64_t size)
{
    if (hook == NULL) {
        LOG_ERROR("Hook is NULL.");
        return ENPU_FAIL;
    }

    rtDrvMemHandle handle;
    int ret = swap_executor_swap_in(hook->executor, ptr, 0, &size, &handle, NOT_MEMCPY_FLAG);
    CHECK_RETURN_ERROR_CODE(ret, "Malloc and map physical mem failed when malloc mem.");

    ret = memory_tracker_add(hook->tracker, *ptr, size, handle, false);
    CHECK_RETURN_ERROR_CODE(ret, "Memory tracker add record failed when malloc mem.");

    shm_state_t *state = get_shm_state();
    vnpu_shm_entry_t *entry = state != NULL ? shm_get_entry(state, get_vnpu_id()) : NULL;
    int my_slot = get_registry_slot();
    if (entry != NULL) {
        if (my_slot >= 0) {
            atomic_fetch_add(&entry->processes[my_slot].hbm_used, size);
        }
        atomic_fetch_add(&entry->hbm_used, size);
    }

    return ENPU_SUCCESS;
}

int swap_hook_malloc_physical_mem(swap_hook_t *hook, rtDrvMemHandle handle, uint64_t size, rtDrvMemProp_t *prop,
                                  uint64_t flags)
{
    if (hook == NULL) {
        LOG_ERROR("Hook is NULL.");
        return ENPU_FAIL;
    }

    int ret = memory_tracker_add(hook->tracker, handle, size, handle, true);
    CHECK_RETURN_ERROR_CODE(ret, "Memory tracker add record failed when malloc physical mem.");
    rtDrvMemProp_t rtProp = {};
    rtProp.side = prop->side;
    rtProp.devid = prop->devid;
    rtProp.module_id = prop->module_id;
    rtProp.pg_type = prop->pg_type;
    rtProp.mem_type = prop->mem_type;
    rtProp.reserve = prop->reserve;
    ret = memory_tracker_add_handle_record(hook->tracker, handle, size, rtProp, flags);
    CHECK_RETURN_ERROR_CODE(ret, "Memory tracker add handle record failed when malloc physical mem.");

    shm_state_t *state = get_shm_state();
    vnpu_shm_entry_t *entry = state != NULL ? shm_get_entry(state, get_vnpu_id()) : NULL;
    int my_slot = get_registry_slot();
    if (entry != NULL) {
        if (my_slot >= 0) {
            atomic_fetch_add(&entry->processes[my_slot].hbm_used, size);
        }
        atomic_fetch_add(&entry->hbm_used, size);
    }

    return ENPU_SUCCESS;
}

int swap_hook_check_and_swap_in(swap_hook_t *hook)
{
    if (hook == NULL) {
        /* swap 未初始化(初始化降级场景): 视为无换入需求, 调用方正常透传, 不阻断业务接口 */
        return ENPU_SUCCESS;
    }

    if (!get_swap_enabled()) {
        return ENPU_SUCCESS;
    }
    pthread_mutex_lock(&hook->tracker->lock);

    int my_slot = get_registry_slot();
    if (my_slot < 0) {
        pthread_mutex_unlock(&hook->tracker->lock);
        return ENPU_SUCCESS;
    }

    if (!shm_get_process_swapped(get_shm_state(), get_vnpu_id(), my_slot)) {
        /* 本进程未处于换出状态, 无需(重新)换入 */
        pthread_mutex_unlock(&hook->tracker->lock);
        return ENPU_SUCCESS;
    }

    LOG_INFO("swap_hook: triggering swap_in for vnpu %d, slot %d.", get_vnpu_id(), my_slot);

    shm_state_t *state = get_shm_state();
    int rc = pthread_mutex_lock(&state->swap_in_mutex);
    if (rc == EOWNERDEAD) {
        pthread_mutex_consistent(&state->swap_in_mutex);
    } else if (rc != 0) {
        LOG_ERROR("Failed to obtain swap in mutex lock, error code=%d.", rc);
        return ENPU_FAIL;
    }
    update_last_kernel_time_now();

    /* 使用本进程的换出量作为换入前的腾挪量估算 */
    process_entry_t *my_entry = &state->entries[get_vnpu_id()].processes[my_slot];
    uint64_t swap_size = my_entry->swap_size;
    int ret = check_and_swap_out(swap_size, SWAP_OUT_FROM_ALL);
    CHECK_RETURN_ERROR_CODE(ret, "Check and swap out failed when swap in.");

    /* 恢复量取进程槽位记录的真实换出量: swap_size 在换出时写为 act_swapped, 与换出侧 clamp 扣减
     * 的 hbm_used 完全同一口径; 恢复逻辑仅对双级 hbm_used 做 atomic_fetch_add(used), 不再逐 record
     * 累加 size, 避免与实拷字节数不一致导致的偏差 */
    uint64_t used = my_entry->swap_size;
    hook->tracker->check_record_in = 0;
    for (size_t i = 0; i < hook->tracker->count; ++i) {
        memory_record_t *record = &(hook->tracker->records[i]);
        if (record->is_swapped == false) {
            continue;
        }
        int ret = ENPU_SUCCESS;
        if (record->is_physical) {
            ret = swap_executor_swap_in_physical(hook->executor, hook->tracker, record->ptr, record->swap_offset,
                                                 &record->size, &record->handle);
        } else {
            ret = swap_executor_swap_in(hook->executor, &record->ptr, record->swap_offset, &record->size,
                                        &record->handle, MEMCPY_FLAG);
        }
        if (ret != ENPU_SUCCESS) {
            LOG_ERROR("swap_hook: swap_in failed for ptr=%p, ret=%d", record->ptr, ret);
            continue;
        }

        ret = memory_tracker_mark_active(hook->tracker, record->ptr, record->handle);
        if (ret != ENPU_SUCCESS) {
            LOG_ERROR("swap_hook: mark_active failed for ptr=%p, ret=%d", record->ptr, ret);
            continue;
        }

        hook->tracker->check_record_in++;
        LOG_DEBUG("swap_hook: swap_in record success for ptr=%p, offset=%lu, size=%lu", record->ptr,
                  record->swap_offset, record->size);
    }

    if (hook->tracker->check_record_in != hook->tracker->check_record) {
        LOG_WARN("swap_hook: check_record mismatch, swap_in=%zu != swap_out=%zu", hook->tracker->check_record_in,
                 hook->tracker->check_record);
    }

    if (used != 0) {
        /* 进程级 + vNPU 级 hbm_used 恢复 */
        atomic_fetch_add(&my_entry->hbm_used, used);
        vnpu_shm_entry_t *vnpu_entry = shm_get_entry(state, get_vnpu_id());
        if (vnpu_entry != NULL) {
            atomic_fetch_add(&vnpu_entry->hbm_used, used);
        }
        shm_set_process_swapped(state, get_vnpu_id(), my_slot, false);

        LOG_INFO("swap_hook: swap_in completed for vnpu %d, vcann_rt_id %d", get_vnpu_id(), my_slot);
    }
    pthread_mutex_unlock(&state->swap_in_mutex);
    pthread_mutex_unlock(&hook->tracker->lock);
    return ENPU_SUCCESS;
}

int swap_hook_free_mem(swap_hook_t *hook, void *ptr)
{
    if (hook == NULL) {
        LOG_ERROR("swap_hook_free_mem failed: hook is NULL");
        return ENPU_FAIL;
    }

    int ret = RUNTIME_HOOK_CALL(rt_library_entry, rtUnmapMem, ptr);
    CHECK_COND_RETURN_(ret != ACL_RT_SUCCESS, ret, "UnmapMem failed when free mem.");

    memory_record_t *record = memory_tracker_find(hook->tracker, ptr);
    CHECK_COND_RETURN_ERROR_CODE(record == NULL, "No record match for devPtr when free mem.");
    uint64_t size = record->size;
    ret = RUNTIME_HOOK_CALL(rt_library_entry, rtFreePhysical, record->handle);
    if (ret == ACL_RT_SUCCESS) {
        ret = RUNTIME_HOOK_CALL(rt_library_entry, rtReleaseMemAddress, ptr);
        CHECK_COND_LOG_(ret != ACL_RT_SUCCESS, "Release va failed when free mem.");
    } else {
        LOG_ERROR("Free physical failed when free mem.");
    }

    ret = memory_tracker_remove(hook->tracker, ptr);
    CHECK_RETURN_ERROR_CODE(ret, "Memory tracker remove record failed when free mem.");

    shm_state_t *state = get_shm_state();
    vnpu_shm_entry_t *entry = state != NULL ? shm_get_entry(state, get_vnpu_id()) : NULL;
    int my_slot = get_registry_slot();
    if (entry != NULL) {
        if (my_slot >= 0) {
            atomic_fetch_sub(&entry->processes[my_slot].hbm_used, size);
        }
        atomic_fetch_sub(&entry->hbm_used, size);
    }

    return ENPU_SUCCESS;
}

int swap_hook_free_physical_mem(swap_hook_t *hook, rtDrvMemHandle handle)
{
    if (hook == NULL) {
        LOG_ERROR("swap_hook_free_physical_mem failed: hook is NULL");
        return ENPU_FAIL;
    }

    memory_record_t *record = memory_tracker_find(hook->tracker, handle);
    // record为NULL时, 表示之前mallocPhysical申请的内存非Device内存, 所以不被记录
    if (record == NULL) {
        return RUNTIME_HOOK_CALL(rt_library_entry, rtFreePhysical, handle);
    }
    uint64_t size = record->size;
    int ret = RUNTIME_HOOK_CALL(rt_library_entry, rtFreePhysical, record->handle);
    CHECK_COND_LOG_(ret != ACL_RT_SUCCESS, "Free physical failed when free physical mem.");

    ret = memory_tracker_remove(hook->tracker, record->ptr);
    CHECK_RETURN_ERROR_CODE(ret, "Memory tracker remove record failed when free physical mem.");
    ret = memory_tracker_remove_handle_record(hook->tracker, handle);
    CHECK_RETURN_ERROR_CODE(ret, "Memory tracker remove handle record failed when free physical mem.");

    shm_state_t *state = get_shm_state();
    vnpu_shm_entry_t *entry = state != NULL ? shm_get_entry(state, get_vnpu_id()) : NULL;
    int my_slot = get_registry_slot();
    if (entry != NULL) {
        if (my_slot >= 0) {
            atomic_fetch_sub(&entry->processes[my_slot].hbm_used, size);
        }
        atomic_fetch_sub(&entry->hbm_used, size);
    }

    return ENPU_SUCCESS;
}

int swap_hook_map_mem(swap_hook_t *hook, void *devPtr, size_t size, size_t offset, rtDrvMemHandle *handle,
                      uint64_t flags)
{
    if (hook == NULL) {
        LOG_ERROR("Swap_hook_map_mem failed, hook is NULL");
        return ENPU_FAIL;
    }

    memory_record_t *record = memory_tracker_find(hook->tracker, *handle);
    // prop为NULL时, 表示之前mallocPhysical申请的内存非Device内存, 所以不被记录
    if (record == NULL) {
        return ENPU_SUCCESS;
    }

    *handle = record->handle;
    return memory_tracker_add_map_record(hook->tracker, devPtr, size, offset, record->ptr, flags);
}

int swap_hook_unmap_mem(swap_hook_t *hook, void *devPtr)
{
    if (hook == NULL) {
        LOG_ERROR("Swap_hook_unmap_mem failed, hook is NULL");
        return ENPU_FAIL;
    }

    return memory_tracker_remove_map_record(hook->tracker, devPtr);
}

int swap_hook_global_init(int vnpu_id)
{
    if (g_global_swap_hook != NULL) {
        LOG_ERROR("swap_hook_global_init failed: already initialized");
        return ENPU_FAIL;
    }

    shm_state_t *state = get_shm_state();
    if (state == NULL) {
        LOG_ERROR("swap_hook_global_init failed: shm_state is NULL");
        return ENPU_FAIL;
    }

    g_swap_buffer_base = swap_buffer_shm_attach();
    if (g_swap_buffer_base == NULL) {
        LOG_ERROR("swap_hook_global_init failed: swap_buffer_shm_attach returned NULL");
        return ENPU_FAIL;
    }

    swap_executor_t *executor = swap_executor_create(g_swap_buffer_base);
    if (executor == NULL) {
        LOG_ERROR("swap_hook_global_init failed: swap_executor_create returned NULL");
        swap_buffer_shm_detach(g_swap_buffer_base);
        g_swap_buffer_base = NULL;
        return ENPU_FAIL;
    }

    memory_tracker_t *tracker = memory_tracker_create(300);
    if (tracker == NULL) {
        LOG_ERROR("swap_hook_global_init failed: memory_tracker_create returned NULL");
        swap_executor_destroy(executor);
        swap_buffer_shm_detach(g_swap_buffer_base);
        g_swap_buffer_base = NULL;
        return ENPU_FAIL;
    }

    g_global_swap_hook = swap_hook_create(vnpu_id, executor, tracker);
    if (g_global_swap_hook == NULL) {
        LOG_ERROR("swap_hook_global_init failed: create returned NULL");
        swap_executor_destroy(executor);
        memory_tracker_destroy(tracker);
        swap_buffer_shm_detach(g_swap_buffer_base);
        g_swap_buffer_base = NULL;
        return ENPU_FAIL;
    }

    vnpu_shm_entry_t *entry = shm_get_entry(state, vnpu_id);
    // request=limit时, 不做显存超分, 不启动监控线程
    if (entry != NULL && get_swap_enabled()) {
        swap_monitor_thread_t **monitor_thread = swap_monitor_get_thread();
        *monitor_thread = swap_monitor_thread_create(vnpu_id, state, executor, tracker, 10);
        if (*monitor_thread == NULL) {
            LOG_WARN("swap_hook_global_init: swap_monitor_thread_create failed, swap monitoring disabled");
            /* 无 monitor 线程的进程无法响应换出命令, 若仍留在注册表中会永久占用
             * swap_cmd.refcount 的递减份额, 使换出完成判定永远无法置位; 此处注销本进程 */
            int slot = get_registry_slot();
            if (slot >= 0) {
                unregister_from_shm_state(state, vnpu_id, slot);
            }
        }
    }

    LOG_INFO("swap_hook_global_init: initialized for vnpu_id=%d", vnpu_id);
    return ENPU_SUCCESS;
}

void swap_hook_global_destroy(void)
{
    swap_monitor_thread_t **monitor_thread = swap_monitor_get_thread();
    if (*monitor_thread != NULL) {
        swap_monitor_thread_stop(*monitor_thread);
        swap_monitor_thread_destroy(*monitor_thread);
        *monitor_thread = NULL;
    }

    if (g_global_swap_hook != NULL) {
        swap_hook_destroy(g_global_swap_hook);
        g_global_swap_hook = NULL;
    }

    if (g_swap_buffer_base != NULL) {
        swap_buffer_shm_detach(g_swap_buffer_base);
        g_swap_buffer_base = NULL;
    }

    int slot = get_registry_slot();
    if (slot >= 0) {
        unregister_from_shm_state(get_shm_state(), get_vnpu_id(), slot);
    }
}

swap_hook_t *swap_hook_get_global(void)
{
    return g_global_swap_hook;
}