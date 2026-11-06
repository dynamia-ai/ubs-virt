/*
 * Copyright (c) Huawei Technologies Co., Ltd. 2025-2026. All rights reserved.
 * enpu-manager is licensed under Mulan PSL v2.
 */

#include "swap_resolver.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include "../internal/npu_allocator.h"
#include "../internal/npu_allocator_internal.h"
#include "log.h"
#include "securec.h"
#include "shm_manager.h"
#include "swap_buffer_manager.h"

#define SWAP_BUFFER_ALIGN_SIZE 4096

struct swap_resolver {
    npu_allocator_t *allocator;
    swap_buffer_manager_t *swap_buf_mgr;
    double pre_swap_threshold;
    uint64_t idle_threshold_ns;
    double last_usage_ratio[MAX_NPU_PER_NODE];
    bool is_usage_logged[MAX_NPU_PER_NODE];
};

swap_resolver_t *swap_resolver_create(npu_allocator_t *allocator, swap_buffer_manager_t *swap_buf_mgr,
                                      double pre_swap_threshold, uint64_t idle_threshold_ns)
{
    if (allocator == NULL || swap_buf_mgr == NULL) {
        return NULL;
    }

    if (pre_swap_threshold < 0 || pre_swap_threshold > 1.0) {
        return NULL;
    }

    swap_resolver_t *resolver = (swap_resolver_t *)calloc(1, sizeof(swap_resolver_t));
    if (resolver == NULL) {
        return NULL;
    }

    resolver->allocator = allocator;
    resolver->swap_buf_mgr = swap_buf_mgr;
    resolver->pre_swap_threshold = pre_swap_threshold;
    resolver->idle_threshold_ns = idle_threshold_ns;

    return resolver;
}

int swap_resolver_destroy(swap_resolver_t *resolver)
{
    if (resolver == NULL) {
        return ENPU_FAIL;
    }

    free(resolver);
    return ENPU_SUCCESS;
}

watermark_level_t swap_resolver_check_watermark(swap_resolver_t *resolver, int phy_id)
{
    if (resolver == NULL || phy_id < 0) {
        return WATERMARK_NORMAL;
    }

    shm_state_t *state = npu_allocator_get_shm_state(resolver->allocator, phy_id);
    if (state == NULL) {
        return WATERMARK_NORMAL;
    }

    uint64_t total_used = 0;

    uint64_t bitmap0 = atomic_load(&state->vnpu_bitmap[0]);
    uint64_t bitmap1 = atomic_load(&state->vnpu_bitmap[1]);

    for (int vnpu_id = 0; vnpu_id < MAX_VNPU_PER_DIE; vnpu_id++) {
        int idx = vnpu_id / 64;
        int bit = vnpu_id % 64;
        uint64_t bitmap = (idx == 0) ? bitmap0 : bitmap1;

        if ((bitmap >> bit) & 1) {
            total_used += atomic_load(&state->entries[vnpu_id].hbm_used);
        }
    }

    if (state->hbm_total == 0) {
        LOG_ERROR("swap_resolver_check_watermark: phy_id=%d hbm_total=0 "
                  "(DCMI may not return HBM size on A5), skip watermark check",
                  phy_id);
        return WATERMARK_NORMAL;
    }

    double usage_ratio = (double)total_used / (double)state->hbm_total;
    double last = resolver->last_usage_ratio[phy_id];
    bool logged = resolver->is_usage_logged[phy_id];
    if (last != usage_ratio || !logged) {
        LOG_INFO("swap_resolver_check_watermark: phy_id=%d total_used=%lu usage_ratio=%.3f", phy_id, total_used,
                 usage_ratio);
        resolver->last_usage_ratio[phy_id] = usage_ratio;
        resolver->is_usage_logged[phy_id] = true;
    }

    if (usage_ratio >= resolver->pre_swap_threshold) {
        return WATERMARK_PRE_SWAP;
    }

    return WATERMARK_NORMAL;
}

int swap_resolver_get_idle_models(swap_resolver_t *resolver, int phy_id, swap_candidate_t *candidates, int max_count,
                                  int *actual_count, uint8_t to_swap_out)
{
    if (resolver == NULL || phy_id < 0) {
        return ENPU_FAIL;
    }

    if (candidates == NULL || max_count <= 0 || actual_count == NULL) {
        return ENPU_FAIL;
    }

    shm_state_t *state = npu_allocator_get_shm_state(resolver->allocator, phy_id);
    if (state == NULL) {
        return ENPU_NOT_FOUND;
    }

    int count = 0;
    uint64_t bitmap0 = atomic_load(&state->vnpu_bitmap[0]);
    uint64_t bitmap1 = atomic_load(&state->vnpu_bitmap[1]);
    // 收集所有已分配的vNPU（含hbm_used=0的）
    for (int vnpu_id = 0; vnpu_id < MAX_VNPU_PER_DIE && count < max_count; vnpu_id++) {
        int idx = vnpu_id / 64;
        int bit = vnpu_id % 64;
        uint64_t bitmap = (idx == 0) ? bitmap0 : bitmap1;

        if (!((bitmap >> bit) & 1)) {
            continue;
        }

        vnpu_shm_entry_t *entry = &state->entries[vnpu_id];
        uint64_t hbm_used = atomic_load(&entry->hbm_used);

        bool swappable = true;
        if (entry->hbm_request == entry->hbm_limit) {
            swappable = false;
        } else if ((to_swap_out == SWAP_OUT_FROM_BORROWED) && (hbm_used <= entry->hbm_request * 1024ULL * 1024ULL)) {
            swappable = false;
        }

        bool swapped = true;
        shm_manager_get_vnpu_swapped(resolver->allocator->shm_mgr, phy_id, vnpu_id, &swapped);

        uint64_t last_kernel_time_ns = 0;
        npu_allocator_get_last_kernel_time_ns(resolver->allocator, phy_id, vnpu_id, &last_kernel_time_ns);

        candidates[count].phy_id = phy_id;
        int print_ret = snprintf_s(candidates[count].shm_id, DIE_ID_LEN, DIE_ID_LEN - 1, "%s", state->shm_id);
        if (print_ret < 0) {
            LOG_ERROR("swap_resolver_get_idle_models: snprintf shm_id failed, ret=%d", print_ret);
            return ENPU_FAIL;
        }
        candidates[count].vnpu_id = vnpu_id;
        candidates[count].sched_policy = entry->sched_policy;
        candidates[count].swap_priority = entry->swap_priority;
        candidates[count].hbm_used = hbm_used;
        candidates[count].last_kernel_time_ns = last_kernel_time_ns;
        candidates[count].swapped = swapped;
        candidates[count].swappable = swappable;

        LOG_INFO("swap_resolver_get_idle_models: pass1 candidate - "
                 "phy_id=%d, vnpu_id=%d, swapped=%d, swappable=%d, "
                 "hbm_used=%lu, last_kernel_time_ns=%lu",
                 phy_id, vnpu_id, swapped ? 1 : 0, swappable ? 1 : 0, (unsigned long)candidates[count].hbm_used,
                 (unsigned long)candidates[count].last_kernel_time_ns);
        count++;
    }

    LOG_INFO("swap_resolver_get_idle_models: phy_id=%d pass1 collected=%d", phy_id, count);

    // 删掉last_kernel_time_ns最大的候选
    if (count > 0) {
        int newest_idx = 0;
        for (int i = 1; i < count; i++) {
            if (candidates[i].last_kernel_time_ns > candidates[newest_idx].last_kernel_time_ns) {
                newest_idx = i;
            }
        }
        LOG_INFO("swap_resolver_get_idle_models: exclude vnpu_id=%d "
                 "(newest last_kernel_time=%lu, hbm_used=%lu, likely just swapped in)",
                 candidates[newest_idx].vnpu_id, (unsigned long)candidates[newest_idx].last_kernel_time_ns,
                 (unsigned long)candidates[newest_idx].hbm_used);
        if (count > 1) {
            candidates[newest_idx] = candidates[count - 1];
        }
        count--;
    }
    // 删掉hbm_used=0、已经被换出的和无法被换出的
    int npu_count = 0;
    for (int i = 0; i < count; i++) {
        if (candidates[i].hbm_used == 0) {
            LOG_INFO("swap_resolver_get_idle_models: pass2 skip vnpu_id=%d (hbm_used=0, no memory to swap out)",
                     candidates[i].vnpu_id);
            continue;
        }
        if (candidates[i].swapped) {
            LOG_INFO("swap_resolver_get_idle_models: pass2 skip vnpu_id=%d (still on CPU side)", candidates[i].vnpu_id);
            continue;
        }
        if (!candidates[i].swappable) {
            LOG_INFO("swap_resolver_get_idle_models: pass2 skip vnpu_id=%d (not swappable: no oversub or no borrow)",
                     candidates[i].vnpu_id);
            continue;
        }
        if (npu_count != i) {
            candidates[npu_count] = candidates[i];
        }
        npu_count++;
    }

    *actual_count = npu_count;
    LOG_INFO("swap_resolver_get_idle_models: phy_id=%d final NPU candidates=%d", phy_id, npu_count);
    return ENPU_SUCCESS;
}

int swap_resolver_select_candidate_from_list(swap_resolver_t *resolver, swap_candidate_t *candidates, int count,
                                             swap_candidate_t *selected)
{
    if (resolver == NULL || candidates == NULL || selected == NULL) {
        return ENPU_FAIL;
    }

    if (count <= 0) {
        return ENPU_NOT_FOUND;
    }

    int best_idx = -1;
    for (int i = 0; i < count; i++) {
        if (best_idx < 0) {
            best_idx = i;
            continue;
        }

        int prio_current = candidates[i].swap_priority;
        int prio_best = candidates[best_idx].swap_priority;

        if (prio_current > prio_best) {
            best_idx = i;
        } else if (prio_current == prio_best) {
            if (candidates[i].last_kernel_time_ns < candidates[best_idx].last_kernel_time_ns) {
                best_idx = i;
            }
        }
    }

    if (best_idx < 0) {
        return ENPU_NOT_FOUND;
    }

    LOG_INFO("swap_resolver_select_candidate_from_list: selected "
             "phy_id=%d, vnpu_id=%d, swap_priority=%d, last_kernel_time_ns=%lu",
             candidates[best_idx].phy_id, candidates[best_idx].vnpu_id, candidates[best_idx].swap_priority,
             candidates[best_idx].last_kernel_time_ns);

    *selected = candidates[best_idx];
    return ENPU_SUCCESS;
}

int swap_resolver_write_swap_command(swap_resolver_t *resolver, int phy_id, const char *pod_uid, int vnpu_id,
                                     int action, uint8_t to_swap_out)
{
    if (resolver == NULL || phy_id < 0) {
        LOG_ERROR("swap_resolver_write_swap_command: invalid params, resolver=%p, phy_id=%d", resolver, phy_id);
        return ENPU_FAIL;
    }

    if (pod_uid == NULL || vnpu_id < 0) {
        LOG_ERROR("swap_resolver_write_swap_command: invalid params, pod_uid=%p, vnpu_id=%d", pod_uid, vnpu_id);
        return ENPU_FAIL;
    }

    if (action < SWAP_ACTION_NONE || action > SWAP_ACTION_IN) {
        LOG_ERROR("swap_resolver_write_swap_command: invalid action=%d", action);
        return ENPU_FAIL;
    }

    if (action == SWAP_ACTION_OUT) {
        shm_state_t *state = npu_allocator_get_shm_state(resolver->allocator, phy_id);
        if (state == NULL) {
            return ENPU_NOT_FOUND;
        }

        if (!atomic_load(&state->swap_cmd.completed)) {
            LOG_INFO("swap_resolver_write_swap_command: previous swap_cmd not completed for phy_id=%d, skip", phy_id);
            return ENPU_SUCCESS;
        }

        vnpu_shm_entry_t *entry = NULL;
        int idx = vnpu_id / 64;
        int bit = vnpu_id % 64;
        uint64_t bitmap = atomic_load(&state->vnpu_bitmap[idx]);

        if ((bitmap >> bit) & 1) {
            entry = &state->entries[vnpu_id];
        }

        if (entry == NULL) {
            LOG_ERROR("swap_resolver_write_swap_command: vnpu_id=%d not found in phy_id=%d", vnpu_id, phy_id);
            return ENPU_NOT_FOUND;
        }

        /* 进程粒度: 统计目标 vNPU 的活跃进程.
         * 注: refcount 由 shm_manager_write_swap_cmd 置为全部活跃进程数(含已 swapped),
         * 已换出的活跃进程通过 cmd_seq guard 恰好递减一次; 仅对 active && !swapped 的进程分配
         * 新的 swap buffer. */
        int active_count = 0;
        for (int i = 0; i < MAX_PROCESSES_PER_VNPU; i++) {
            if (atomic_load(&entry->processes[i].active)) {
                active_count++;
            }
        }

        if (active_count == 0) {
            /* 无活跃进程: 写命令会导致 refcount=0 而 completed 永不置位, 直接跳过 */
            LOG_INFO("swap_resolver_write_swap_command: no active process for vnpu_id=%d, skip swap_out", vnpu_id);
            return ENPU_SUCCESS;
        }

        /* 为每个需要换出 (active && !swapped) 的进程精确分配 swap buffer,
         * 直接读取进程级 hbm_used; 用 bitmask 记录本次已分配的槽位, 失败时回滚避免泄漏 */
        uint64_t allocated_mask = 0;
        for (int i = 0; i < MAX_PROCESSES_PER_VNPU; i++) {
            process_entry_t *pe = &entry->processes[i];
            if (!atomic_load(&pe->active)) {
                continue;
            }
            if (atomic_load(&pe->swapped)) {
                continue;
            }

            uint64_t process_used = atomic_load(&pe->hbm_used);
            if (process_used == 0) {
                /* 该进程尚未占用显存, 无需 swap buffer, 避免 tlsf_alloc(0) 必然失败
                 * 导致整个 vNPU 的换出被判定失败 */
                LOG_INFO("swap_resolver_write_swap_command: skip slot=%d of vnpu_id=%d, no hbm used", i, vnpu_id);
                continue;
            }
            uint64_t aligned = (process_used + SWAP_BUFFER_ALIGN_SIZE - 1) & ~(uint64_t)(SWAP_BUFFER_ALIGN_SIZE - 1);
            int ret = shm_alloc_per_process(state, vnpu_id, i, aligned);
            if (ret != ENPU_SUCCESS) {
                LOG_ERROR("swap_resolver_write_swap_command: shm_alloc_per_process failed, "
                          "phy_id=%d, vnpu=%d, slot=%d, size=%lu, ret=%d",
                          phy_id, vnpu_id, i, aligned, ret);

                /* 回滚本次已成功分配的槽位, 释放 swap buffer, 避免泄漏 */
                for (int j = 0; j < MAX_PROCESSES_PER_VNPU; j++) {
                    if ((allocated_mask >> j) & 1ULL) {
                        (void)shm_free_per_process(state, vnpu_id, j);
                        LOG_WARN("swap_resolver_write_swap_command: rollback slot=%d for phy_id=%d, vnpu_id=%d", j,
                                 phy_id, vnpu_id);
                    }
                }
                return ret;
            }
            allocated_mask |= 1ULL << i;
            LOG_INFO("swap_resolver: shm_alloc_per_process for phy_id=%d, vnpu_id=%d, slot=%d, "
                     "process_used=%lu, alloc_size=%lu",
                     phy_id, vnpu_id, i, process_used, aligned);
        }
    }

    return npu_allocator_write_swap_cmd(resolver->allocator, phy_id, pod_uid, vnpu_id, action, to_swap_out);
}

int swap_resolver_check_and_swap_in(swap_resolver_t *resolver, int phy_id)
{
    if (resolver == NULL) {
        LOG_ERROR("Invalid params, resolver=%p.", resolver);
        return ENPU_FAIL;
    }

    shm_state_t *state = npu_allocator_get_shm_state(resolver->allocator, phy_id);
    if (state == NULL) {
        return ENPU_SUCCESS;
    }

    if (!atomic_load(&state->swap_cmd.completed)) {
        LOG_INFO("Swap out working.");
        return ENPU_SUCCESS;
    }

    uint64_t bitmap0 = atomic_load(&state->vnpu_bitmap[0]);
    uint64_t bitmap1 = atomic_load(&state->vnpu_bitmap[1]);

    for (int vnpu_id = 0; vnpu_id < MAX_VNPU_PER_DIE; vnpu_id++) {
        int idx = vnpu_id / 64;
        int bit = vnpu_id % 64;
        uint64_t bitmap = (idx == 0) ? bitmap0 : bitmap1;

        if ((bitmap >> bit) & 1) {
            vnpu_shm_entry_t *vnpu_entry = &state->entries[vnpu_id];

            /* 进程粒度: 遍历该 vNPU 的所有活跃且未换出但已分配了 swap buffer 的进程,
             * 逐一释放各自的 swap buffer. 各进程 swap_size 记录在 processes[slot] 上. */
            for (int slot = 0; slot < MAX_PROCESSES_PER_VNPU; slot++) {
                process_entry_t *pe = &vnpu_entry->processes[slot];
                if (!atomic_load(&pe->active)) {
                    continue;
                }
                if (!atomic_load(&pe->swapped) && pe->swap_size != 0) {
                    LOG_INFO("Free swap buffer for phy_id=%d vnpu_id=%d slot=%d.", phy_id, vnpu_id, slot);
                    int ret = shm_free_per_process(state, vnpu_id, slot);
                    if (ret != ENPU_SUCCESS) {
                        LOG_ERROR("Free swap buffer for phy_id=%d vnpu_id=%d slot=%d failed.", phy_id, vnpu_id, slot);
                    }
                }
            }
        }
    }
    return ENPU_SUCCESS;
}

int swap_resolver_check_enpu_swap_out(swap_resolver_t *resolver, int phy_id, uint8_t *flag)
{
    if (resolver == NULL) {
        LOG_ERROR("Invalid params, resolver=%p.", resolver);
        return ENPU_FAIL;
    }

    shm_state_t *state = npu_allocator_get_shm_state(resolver->allocator, phy_id);
    if (state == NULL) {
        return ENPU_SUCCESS;
    }

    *flag = atomic_load(&state->swap_out_cmd.action);
    if (*flag != SWAP_OUT_NONE) {
        atomic_store(&state->swap_out_cmd.action, SWAP_OUT_NONE);
    }
    return ENPU_SUCCESS;
}

bool swap_resolver_is_swap_pending(swap_resolver_t *resolver, int phy_id)
{
    if (resolver == NULL || resolver->allocator == NULL || phy_id < 0) {
        return false;
    }

    shm_state_t *state = npu_allocator_get_shm_state(resolver->allocator, phy_id);
    if (state == NULL) {
        return false;
    }
    return !atomic_load(&state->swap_cmd.completed);
}