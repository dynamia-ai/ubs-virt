/*
 * Copyright (c) Huawei Technologies Co., Ltd. 2026-2026. All rights reserved.
 * ubs-virt-enpu is licensed under Mulan PSL v2.
 */

#include "swap_monitor_thread.h"
#include <pthread.h>
#include <stdatomic.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include "log.h"
#include "npu_manager.h"
#include "shm_manager.h"

swap_monitor_thread_t *g_swap_monitor_thread = NULL;

swap_monitor_thread_t **swap_monitor_get_thread(void)
{
    return &g_swap_monitor_thread;
}

/* 换出完成应答: 用 atomic_exchange 抢占本槽位的 ack_pending_seq, 保证每个进程对同一 epoch
 * 恰好 ack 一次(与 watchdog 代 ack 互斥); 全部槽位都已 ack 时置 completed. */
static void swap_ack_and_decrement(swap_monitor_thread_t *mon_thread, int32_t cmd_vnpu_id, uint64_t seq)
{
    int remain = shm_try_ack_swap_cmd(mon_thread->shm_state, cmd_vnpu_id, get_registry_slot(), seq);
    if (remain < 0) {
        /* 本槽位已不欠本 epoch 的 ack: 被 watchdog 代 ack 过, 或命令已收尾, 不重复递减 */
        LOG_INFO("swap_monitor_thread: ack skipped for vnpu_id=%d seq=%lu (already satisfied)", cmd_vnpu_id,
                 (unsigned long)seq);
        return;
    }

    if (remain == 0) {
        LOG_INFO("swap_monitor_thread: last process, swap_out completed for vnpu_id=%d seq=%lu", cmd_vnpu_id,
                 (unsigned long)seq);
    } else {
        LOG_INFO("swap_monitor_thread: swap_out done, refcount %d->%d, waiting for others", remain + 1, remain);
    }
}

static void *swap_monitor_thread_func(void *arg)
{
    swap_monitor_thread_t *mon_thread = (swap_monitor_thread_t *)arg;
    int phy_id = atomic_load(&mon_thread->shm_state->phy_id);
    int ret = RUNTIME_HOOK_CALL(rt_library_entry, rtSetDevice, get_device_id());
    LOG_INFO("swap_monitor_thread_func: set device to phy_id=%d, vnpu_id=%d, ret=%d", phy_id, mon_thread->vnpu_id, ret);

    /* 进程级换出：定位本进程的注册槽位（无槽位则无法执行进程级换出） */
    int my_slot = get_registry_slot();
    if (my_slot < 0) {
        LOG_WARN("swap_monitor_thread_func: no registry slot for vnpu_id=%d, swap-out disabled", mon_thread->vnpu_id);
        return NULL;
    }
    process_entry_t *my_entry = &mon_thread->shm_state->entries[mon_thread->vnpu_id].processes[my_slot];
    atomic_store(&my_entry->swapped, false);

    while (atomic_load(&mon_thread->running)) {
        int32_t cmd_vnpu_id = 0;
        int32_t action = SWAP_ACTION_NONE;

        int rc = shm_check_swap_cmd(mon_thread->shm_state, &cmd_vnpu_id, &action);
        if (rc != 0) {
            usleep(mon_thread->poll_interval_ms * 1000);
            continue;
        }

        if (action == SWAP_ACTION_NONE) {
            usleep(mon_thread->poll_interval_ms * 1000);
            continue;
        }

        if (cmd_vnpu_id != mon_thread->vnpu_id) {
            usleep(mon_thread->poll_interval_ms * 1000);
            continue;
        }

        /* 换入由 swap_hook 懒触发, monitor 线程不处理换入动作 */
        if (action == SWAP_ACTION_IN) {
            usleep(mon_thread->poll_interval_ms * 1000);
            continue;
        }

        if (action == SWAP_ACTION_OUT) {
            /* cmd_seq guard: 仅处理注册之后下发的命令, 且每个命令本进程恰好处理/递减一次 */
            uint64_t current_seq = atomic_load(&mon_thread->shm_state->swap_cmd.cmd_seq);
            if (current_seq == get_last_seen_cmd_seq()) {
                usleep(mon_thread->poll_interval_ms * 1000);
                continue;
            }
            set_last_seen_cmd_seq(current_seq);

            if (!atomic_load(&my_entry->swapped)) {
                LOG_INFO("swap_monitor_thread: detected swap_cmd action=%d for vnpu_id=%d, starting swap_out", action,
                         cmd_vnpu_id);

                vnpu_shm_entry_t *entry = shm_get_entry(mon_thread->shm_state, mon_thread->vnpu_id);
                if (entry == NULL) {
                    LOG_ERROR("swap_monitor_thread: failed to get entry for vnpu_id=%d", cmd_vnpu_id);
                    swap_ack_and_decrement(mon_thread, cmd_vnpu_id, current_seq);
                    usleep(mon_thread->poll_interval_ms * 1000);
                    continue;
                }

                /* 锁冻结：换出期间该进程无法再分配/释放显存 */
                pthread_mutex_lock(&mon_thread->tracker->lock);

                memory_record_t **records = NULL;
                int count = 0;

                ret = memory_tracker_collect_records(mon_thread->tracker, &records, &count);
                if (ret != ENPU_SUCCESS || count == 0) {
                    LOG_WARN("swap_monitor_thread: collect_records failed or no records, ret=%d, count=%d", ret, count);
                    if (records != NULL) {
                        free(records);
                    }
                    pthread_mutex_unlock(&mon_thread->tracker->lock);
                    swap_ack_and_decrement(mon_thread, cmd_vnpu_id, current_seq);
                    usleep(mon_thread->poll_interval_ms * 1000);
                    continue;
                }

                uint64_t act_swapped = 0;
                uint64_t swap_offset = my_entry->swap_offset;
                swap_executor_swap_out(mon_thread->executor, mon_thread->tracker, records, count, swap_offset,
                                       &act_swapped);
                free(records);

                if (act_swapped > 0) {
                    /* 记录本进程真实的换出量（enpu-manager 依此记账） */
                    my_entry->swap_size = act_swapped;

                    /* clamp 扣减进程级与 vNPU 级 hbm_used, 防下溢到巨大值 */
                    uint64_t my_used = atomic_load(&my_entry->hbm_used);
                    uint64_t dec_proc = (act_swapped < my_used) ? act_swapped : my_used;
                    if (dec_proc > 0) {
                        atomic_fetch_sub(&my_entry->hbm_used, dec_proc);
                    }
                    uint64_t vnpu_used = atomic_load(&entry->hbm_used);
                    uint64_t dec_vnpu = (act_swapped < vnpu_used) ? act_swapped : vnpu_used;
                    if (dec_vnpu > 0) {
                        atomic_fetch_sub(&entry->hbm_used, dec_vnpu);
                    }

                    atomic_store(&my_entry->swapped, true);
                } else {
                    /* 一个字节都没拷成功: 不能把 swap_size 覆盖成 0, 否则 manager 侧的
                     * "swap_size != 0 即 buffer 已分配"判据失效, TLSF 块会永久泄漏;
                     * 同时也不置 swapped, 避免进程被永久视为已换出而无法再换入 */
                    LOG_WARN("swap_monitor_thread: swap_out copied nothing, vnpu_id=%d slot=%d, "
                             "keep allocated swap_size=%lu",
                             mon_thread->vnpu_id, my_slot, my_entry->swap_size);
                }

                pthread_mutex_unlock(&mon_thread->tracker->lock);

                LOG_INFO("swap_monitor_thread: swap_out completed, count=%d, offset=%lu, act_swapped=%lu", count,
                         swap_offset, act_swapped);
            }

            swap_ack_and_decrement(mon_thread, cmd_vnpu_id, current_seq);
        }

        usleep(mon_thread->poll_interval_ms * 1000);
    }

    LOG_INFO("swap_monitor_thread stopped for vnpu_id=%d", mon_thread->vnpu_id);
    return NULL;
}

swap_monitor_thread_t *swap_monitor_thread_create(int vnpu_id, shm_state_t *shm_state, void *executor, void *tracker,
                                                  uint64_t poll_interval_ms)
{
    if (shm_state == NULL || executor == NULL || tracker == NULL) {
        LOG_ERROR("swap_monitor_thread_create failed: shm_state=%p, executor=%p, tracker=%p", shm_state, executor,
                  tracker);
        return NULL;
    }

    if (vnpu_id < 0 || poll_interval_ms == 0) {
        LOG_ERROR("swap_monitor_thread_create failed: invalid vnpu_id=%d or poll_interval_ms=%lu", vnpu_id,
                  poll_interval_ms);
        return NULL;
    }

    swap_monitor_thread_t *thread = (swap_monitor_thread_t *)calloc(1, sizeof(swap_monitor_thread_t));
    if (thread == NULL) {
        LOG_ERROR("swap_monitor_thread_create failed: calloc returned NULL");
        return NULL;
    }

    thread->vnpu_id = vnpu_id;
    thread->shm_state = shm_state;
    thread->executor = (swap_executor_t *)executor;
    thread->tracker = (memory_tracker_t *)tracker;
    thread->poll_interval_ms = poll_interval_ms;
    atomic_store(&thread->running, false);
    atomic_store(&thread->stop_requested, false);

    return thread;
}

int swap_monitor_thread_destroy(swap_monitor_thread_t *thread)
{
    if (thread == NULL) {
        return ENPU_FAIL;
    }

    free(thread);
    return ENPU_SUCCESS;
}

int swap_monitor_thread_start(swap_monitor_thread_t *thread)
{
    if (thread == NULL) {
        LOG_ERROR("swap_monitor_thread_start failed: thread is NULL");
        return ENPU_FAIL;
    }

    atomic_store(&thread->running, true);
    atomic_store(&thread->stop_requested, false);

    int ret = pthread_create(&thread->thread, NULL, swap_monitor_thread_func, thread);
    if (ret != 0) {
        LOG_ERROR("swap_monitor_thread_start failed: pthread_create returned %d", ret);
        atomic_store(&thread->running, false);
        return ENPU_FAIL;
    }

    return ENPU_SUCCESS;
}

int swap_monitor_thread_stop(swap_monitor_thread_t *thread)
{
    if (thread == NULL) {
        return ENPU_FAIL;
    }

    atomic_store(&thread->running, false);

    pthread_join(thread->thread, NULL);

    return ENPU_SUCCESS;
}
