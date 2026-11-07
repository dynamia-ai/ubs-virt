/*
 * Copyright (c) Huawei Technologies Co., Ltd. 2025-2026. All rights reserved.
 * enpu-manager is licensed under Mulan PSL v2.
 */

#include "pod_watchdog.h"
#include <stdatomic.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include "../internal/npu_allocator.h"
#include "log.h"
#include "shm_manager.h"
#include "swap_buffer_manager.h"

struct pod_watchdog {
    shm_manager_t *shm_mgr;
    swap_buffer_manager_t *swap_buf_mgr;
    npu_allocator_t *allocator;
};

static uint64_t wd_now_ns(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint64_t)ts.tv_sec * 1000000000ULL + (uint64_t)ts.tv_nsec;
}

pod_watchdog_t *pod_watchdog_create(void *shm_mgr, void *swap_buf_mgr, void *allocator)
{
    if (shm_mgr == NULL || swap_buf_mgr == NULL) {
        return NULL;
    }

    pod_watchdog_t *wd = (pod_watchdog_t *)calloc(1, sizeof(pod_watchdog_t));
    if (wd == NULL) {
        return NULL;
    }

    wd->shm_mgr = (shm_manager_t *)shm_mgr;
    wd->swap_buf_mgr = (swap_buffer_manager_t *)swap_buf_mgr;
    wd->allocator = (npu_allocator_t *)allocator;

    return wd;
}

int pod_watchdog_destroy(pod_watchdog_t *wd)
{
    if (wd == NULL) {
        return ENPU_FAIL;
    }

    free(wd);
    return ENPU_SUCCESS;
}

int pod_watchdog_cleanup_swap_data(pod_watchdog_t *wd, int phy_id, int vnpu_id)
{
    if (wd == NULL) {
        return ENPU_FAIL;
    }

    bool swapped = false;
    uint64_t offset = 0, size = 0;

    int ret = shm_manager_get_swap_state(wd->shm_mgr, phy_id, vnpu_id, &swapped, &offset, &size);
    if (ret == ENPU_NOT_FOUND) {
        return ENPU_NOT_FOUND;
    }
    if (ret != ENPU_SUCCESS) {
        return ret;
    }

    if (!swapped) {
        return ENPU_SUCCESS;
    }

    shm_state_t *state = shm_manager_get_state(wd->shm_mgr, phy_id);
    if (state != NULL) {
        shm_free(state, vnpu_id);
    }

    ret = shm_manager_set_swap_state(wd->shm_mgr, phy_id, vnpu_id, 0, 0);
    if (ret != ENPU_SUCCESS) {
        return ret;
    }

    return ENPU_SUCCESS;
}

static void reset_runtime_state(shm_state_t *state, vnpu_time_slice_sched_t *sched, int vnpu_id)
{
    vnpu_shm_entry_t *entry = &state->entries[vnpu_id];
    atomic_store(&entry->hbm_used, 0);

    /* 进程粒度: 清理该 vNPU 的进程注册表, 避免死进程槽位/计数持续污染水位 */
    for (int slot = 0; slot < MAX_PROCESSES_PER_VNPU; slot++) {
        process_entry_t *pe = &entry->processes[slot];
        atomic_store(&pe->swapped, false);
        atomic_store(&pe->active, false);
        atomic_store(&pe->hbm_used, 0);
        atomic_store(&pe->last_heartbeat_ns, 0);
        pe->swap_offset = 0;
        pe->swap_size = 0;
        atomic_store(&pe->ack_pending_seq, 0);
    }

    if (sched != NULL) {
        atomic_store(&sched->last_alive_time_ns[vnpu_id], 0);
    }
}

/* 进程级存活检测: 心跳超时 (PROCESS_ALIVE_TIMEOUT_NS) 的僵死进程, 回收其
 * hbm_used 余额与 swap buffer, 并释放槽位. 与正常注销 (unregister_from_shm_state)
 * 幂等兼容, 两者对 active/hbm_used 的操作均为 atomic_store/atomic_fetch_sub.
 * 回收前先代死进程 ack 换出命令: 槽位一旦被清空就再也无法判断它是否欠一次 ack. */
static void reap_dead_processes(shm_manager_t *shm_mgr, shm_state_t *state, int phy_id, uint64_t now_ns,
                                uint64_t timeout_ns)
{
    if (state == NULL) {
        return;
    }

    for (int v = 0; v < MAX_VNPU_PER_DIE; v++) {
        vnpu_shm_entry_t *vnpu_entry = &state->entries[v];
        for (int i = 0; i < MAX_PROCESSES_PER_VNPU; i++) {
            process_entry_t *pe = &vnpu_entry->processes[i];
            if (!atomic_load(&pe->active)) {
                /* 已注销进程遗留的 swap buffer: 槽位已无主且不会再换入, 由 manager 侧回收
                 * (TLSF 池由 manager 独占, vcann-rt 无法释放), 避免进程反复启停耗尽换出池 */
                if (atomic_load(&pe->ack_pending_seq) != 0) {
                    (void)shm_manager_proxy_ack_process(shm_mgr, phy_id, v, i);
                }
                if (pe->swap_size > 0) {
                    LOG_WARN("[POD-WATCHDOG] reclaim orphan swap buffer vnpu_id=%d slot=%d size=%llu", v, i,
                             (unsigned long long)pe->swap_size);
                    (void)shm_free_per_process(state, v, i);
                }
                continue;
            }

            uint64_t hb = atomic_load(&pe->last_heartbeat_ns);
            if (hb == 0) {
                continue; /* 已注册但尚未开始刷新, 不误判 */
            }

            /* 时钟重置保护: 系统重启后 CLOCK_MONOTONIC 归零, 与 vnpu 级保护 (line 144) 一致 */
            if (hb > now_ns) {
                LOG_WARN("[POD-WATCHDOG] vnpu_id=%d slot=%d hb > now "
                         "(clock reset after reboot?), recalibrating to 0",
                         v, i);
                atomic_store(&pe->last_heartbeat_ns, 0);
                continue;
            }

            if (now_ns - hb <= timeout_ns) {
                continue; /* 仍活 */
            }

            LOG_WARN("[POD-WATCHDOG] reap dead process vcann_rt_id=%d vnpu_id=%d slot=%d (hb_age=%llums)",
                     pe->vcann_rt_id, v, i, (unsigned long long)((now_ns - hb) / 1000000ULL));

            /* 0. 代死进程 ack 换出命令: 必须在清理 swapped/active 之前, 否则槽位被复用后
             * 就再也没有依据判断它是否欠过一次 ack (幂等, 仅 marker==cmd_seq 时生效) */
            if (atomic_load(&pe->ack_pending_seq) != 0) {
                (void)shm_manager_proxy_ack_process(shm_mgr, phy_id, v, i);
            }

            /* 1. 释放 hbm_used 余额, 扣减 vnpu 级计数 */
            uint64_t leaked = atomic_load(&pe->hbm_used);
            if (leaked > 0) {
                atomic_fetch_sub(&vnpu_entry->hbm_used, leaked);
                atomic_store(&pe->hbm_used, 0);
            }

            /* 2. 释放 swap buffer: swap_size != 0 表示 manager 已分配 buffer(可能尚未完成换出),
             * 只要求 swapped 会在"已分配未换出"时漏回收, 导致 TLSF 块永久泄漏 */
            if (pe->swap_size > 0) {
                (void)shm_free_per_process(state, v, i);
            }

            /* 3. 清理标志并释放槽位, 允许新进程复用 */
            atomic_store(&pe->swapped, false);
            atomic_store(&pe->active, false);
        }
    }
}

int pod_watchdog_poll_and_cleanup(pod_watchdog_t *wd)
{
    if (wd == NULL) {
        return ENPU_FAIL;
    }

    uint64_t now_ns = wd_now_ns();

    for (int phy_id = 0; phy_id < MAX_NPU_PER_NODE; phy_id++) {
        shm_state_t *state = wd->shm_mgr->shm_states[phy_id];
        if (state == NULL) {
            continue;
        }

        vnpu_time_slice_sched_t *sched = shm_manager_get_sched_struct(wd->shm_mgr, phy_id);
        if (sched == NULL) {
            LOG_WARN("[POD-WATCHDOG] phy_id=%d sched_struct missing, skip all vNPUs on this DIE", phy_id);
            continue;
        }

        uint64_t bitmap0 = atomic_load(&state->vnpu_bitmap[0]);
        uint64_t bitmap1 = atomic_load(&state->vnpu_bitmap[1]);

        for (int vnpu_id = 0; vnpu_id < MAX_VNPU_PER_DIE; vnpu_id++) {
            int idx = vnpu_id / 64;
            int bit = vnpu_id % 64;
            uint64_t bitmap = (idx == 0) ? bitmap0 : bitmap1;

            if (!((bitmap >> bit) & 1)) {
                continue;
            }

            uint64_t last_alive = atomic_load(&sched->last_alive_time_ns[vnpu_id]);
            if (last_alive == 0) {
                continue; /* vcann-rt never started for this vnpu, not dead */
            }

            /* uint64 下溢保护：系统重启后 CLOCK_MONOTONIC 归零，但 shm 保留上次的
             * last_alive（比当前 now_ns 大），相减会下溢到接近 uint64_max。
             * 这种情况是"时钟重置"而非"vcann-rt 死亡"，清零 last_alive 并跳过。 */
            if (last_alive > now_ns) {
                LOG_WARN("[POD-WATCHDOG] phy_id=%d vnpu_id=%d last_alive > now "
                         "(clock reset after reboot?), recalibrating to 0",
                         phy_id, vnpu_id);
                atomic_store(&sched->last_alive_time_ns[vnpu_id], 0);
                continue;
            }

            if (now_ns - last_alive <= POD_ALIVE_TIMEOUT_NS) {
                continue; /* still alive */
            }

            /* 清理超时的 vNPU 的 swap 资源和运行时资源 */
            bool was_swapped = false;
            (void)shm_manager_get_vnpu_all_swapped(wd->shm_mgr, phy_id, vnpu_id, &was_swapped);

            LOG_WARN("[POD-WATCHDOG] phy_id=%d vnpu_id=%d timeout (last_alive %.3fs ago > %.3fs); "
                     "cleaning runtime state (was_swapped=%d)",
                     phy_id, vnpu_id, (now_ns - last_alive) / 1000000000.0, POD_ALIVE_TIMEOUT_NS / 1000000000.0,
                     was_swapped);

            if (was_swapped) {
                int ret = pod_watchdog_cleanup_swap_data(wd, phy_id, vnpu_id);
                if (ret != ENPU_SUCCESS && ret != ENPU_NOT_FOUND) {
                    LOG_ERROR("[POD-WATCHDOG] cleanup_swap_data failed: phy_id=%d vnpu_id=%d ret=%d", phy_id, vnpu_id,
                              ret);
                }
            }

            /* 整个 vNPU 已失联: 先为所有仍欠 ack 的槽位代 ack, 再清空运行时状态,
             * 否则 ack 依据会随槽位一起被清零 */
            for (int slot = 0; slot < MAX_PROCESSES_PER_VNPU; slot++) {
                if (atomic_load(&state->entries[vnpu_id].processes[slot].ack_pending_seq) != 0) {
                    (void)shm_manager_proxy_ack_process(wd->shm_mgr, phy_id, vnpu_id, slot);
                }
            }

            reset_runtime_state(state, sched, vnpu_id);
        }

        /* 进程级存活检测: 周期回收心跳超时的僵死进程 (独立于 swap_cmd, 见方案 §4.4.3) */
        reap_dead_processes(wd->shm_mgr, state, phy_id, now_ns, PROCESS_ALIVE_TIMEOUT_NS);

        /* 兜底完成判定: 覆盖"进程在 ack 成功后消失、槽位被复用"等逐进程代 ack 无法收敛的残留;
         * 无待办 marker 说明所有参与者都已 ack, 命令必须收尾, 否则 die 级换出线程永久停摆 */
        (void)shm_manager_try_complete_swap_cmd(wd->shm_mgr, phy_id);
    }

    return ENPU_SUCCESS;
}