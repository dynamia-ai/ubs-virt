/*
 * Copyright (c) Huawei Technologies Co., Ltd. 2025-2026. All rights reserved.
 * enpu-manager is licensed under Mulan PSL v2.
 * You can use this software according to the terms and conditions of the Mulan PSL v2.
 * You may obtain a copy of Mulan PSL v2 at:
 *          http://license.coscl.org.cn/MulanPSL2
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND,
 * EITHER EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT,
 * MERCHANTABILITY OR FIT FOR A PARTICULAR PURPOSE.
 * See the Mulan PSL v2 for more details.
 */

#include "shm_manager.h"
#include <errno.h>
#include <fcntl.h>
#include <pthread.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <time.h>
#include "log.h"
#include "securec.h"

#define MAX_RETRY_COUNT 3
#define SHM_SWAP_BUFFER_NAME "/shm_swap_buffer"
#define TLSF_ALIGN 64
#define TLSF_MAX_FL 30
#define TLSF_MAX_SL 16

int shm_id_format_standardize(int phy_id, const char *die_id_str, char *buf, int buf_len)
{
    if (phy_id < 0 || die_id_str == NULL || buf == NULL || buf_len <= 0) {
        return ENPU_INVALID_PARAM;
    }

    int written = snprintf_s(buf, buf_len, buf_len - 1, "/die-%d-%s", phy_id, die_id_str);
    if (written < 0 || written >= buf_len) {
        return ENPU_FAIL;
    }

    return ENPU_SUCCESS;
}

int shm_struct_init(const char *shm_id, vnpu_time_slice_sched_t **sched_out)
{
    if (shm_id == NULL || sched_out == NULL) {
        return ENPU_INVALID_PARAM;
    }

    int fd = shm_open(shm_id, O_CREAT | O_RDWR, S_IRUSR | S_IWUSR);
    if (fd == -1) {
        return ENPU_FAIL;
    }

    size_t size = sizeof(vnpu_time_slice_sched_t);
    struct stat st;
    if (fstat(fd, &st) == 0 && st.st_size >= size) {
        void *addr = mmap(NULL, size, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
        close(fd);

        if (addr == MAP_FAILED) {
            shm_unlink(shm_id);
            return ENPU_FAIL;
        }

        vnpu_time_slice_sched_t *sched = (vnpu_time_slice_sched_t *)addr;
        if (atomic_load(&sched->magic_number) == MAGIC_INITIALIZED) {
            *sched_out = sched;
            return ENPU_SUCCESS;
        }

        munmap(addr, size);
        fd = shm_open(shm_id, O_RDWR, S_IRUSR | S_IWUSR);
        if (fd == -1) {
            return ENPU_FAIL;
        }
    }

    if (ftruncate(fd, size) == -1) {
        close(fd);
        shm_unlink(shm_id);
        return ENPU_FAIL;
    }

    void *addr = mmap(NULL, size, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
    close(fd);

    if (addr == MAP_FAILED) {
        shm_unlink(shm_id);
        return ENPU_FAIL;
    }

    vnpu_time_slice_sched_t *sched = (vnpu_time_slice_sched_t *)addr;

    pthread_mutexattr_t attr;
    pthread_mutexattr_init(&attr);
    pthread_mutexattr_setpshared(&attr, PTHREAD_PROCESS_SHARED);
    pthread_mutexattr_setrobust(&attr, PTHREAD_MUTEX_ROBUST);

    for (int i = 0; i < MAX_VNPU_SCHED; i++) {
        pthread_mutex_init(&sched->vnpu_schedule_mutex[i], &attr);
        atomic_store(&sched->last_alive_time_ns[i], 0ULL);
        atomic_store(&sched->last_kernel_time_ns[i], 0ULL);
        atomic_store(&sched->vnpu_schedule_turn[i], 0);
        atomic_store(&sched->vnpu_core_limit_quota[i], 0);
        atomic_store(&sched->vnpu_priority[i], VNPU_PRIORITY_NORMAL);
        atomic_store(&sched->vnpu_quota_timeslice[i], 0ULL);
        atomic_store(&sched->vnpu_cur_timeslice[i], 0ULL);
        atomic_store(&sched->vnpu_quota_injected_for_turn[i], (uint_fast8_t)0xFF);
        atomic_store(&sched->vnpu_sync_expected[i], 0);
        atomic_store(&sched->vnpu_sync_completed[i], 0);
        atomic_store(&sched->vnpu_turn_max_sync[i], 0ULL);
        atomic_store(&sched->vnpu_timeslice_for_turn[i], 0ULL);
    }

    pthread_mutex_init(&sched->npu_utilization_monitor_mutex, &attr);

    pthread_mutexattr_destroy(&attr);

    atomic_store(&sched->owner, -1);
    atomic_store(&sched->magic_number, MAGIC_INITIALIZED);
    atomic_store(&sched->slide_window_len, 0);
    atomic_store(&sched->last_slide_window_time_ns, 0ULL);

    *sched_out = sched;
    return ENPU_SUCCESS;
}

int shm_struct_cleanup(const char *shm_id, vnpu_time_slice_sched_t *sched, int vnpu_count)
{
    if (shm_id == NULL) {
        return ENPU_INVALID_PARAM;
    }

    if (vnpu_count > 0) {
        return ENPU_SUCCESS;
    }

    if (sched != NULL) {
        for (int i = 0; i < MAX_VNPU_SCHED; i++) {
            pthread_mutex_destroy(&sched->vnpu_schedule_mutex[i]);
        }
        munmap(sched, sizeof(vnpu_time_slice_sched_t));
    }

    shm_unlink(shm_id);
    return ENPU_SUCCESS;
}

static void destroy_shm_state(shm_state_t *state)
{
    if (state != NULL) {
        pthread_mutex_destroy(&state->swap_in_mutex);
        shm_state_posix_shm_destroy(state->phy_id, state->shm_id);
        munmap(state, sizeof(shm_state_t));
    }
}

shm_manager_t *shm_manager_create(void)
{
    shm_manager_t *mgr = (shm_manager_t *)calloc(1, sizeof(shm_manager_t));
    if (mgr == NULL) {
        return NULL;
    }

    if (pthread_mutex_init(&mgr->lock, NULL) != 0) {
        free(mgr);
        return NULL;
    }

    mgr->shm_state_count = 0;
    mgr->initialized = false;
    (void)memset_s(mgr->shm_states, sizeof(mgr->shm_states), 0, sizeof(mgr->shm_states));
    (void)memset_s(mgr->sched_structs, sizeof(mgr->sched_structs), 0, sizeof(mgr->sched_structs));

    mgr->initialized = true;
    return mgr;
}

vnpu_time_slice_sched_t *shm_manager_get_sched_struct(shm_manager_t *mgr, int phy_id)
{
    if (mgr == NULL || phy_id < 0 || phy_id >= MAX_NPU_PER_NODE) {
        return NULL;
    }
    return mgr->sched_structs[phy_id];
}

int shm_manager_destroy(shm_manager_t *mgr)
{
    if (mgr == NULL) {
        return ENPU_INVALID_PARAM;
    }

    pthread_mutex_lock(&mgr->lock);

    for (int i = 0; i < MAX_NPU_PER_NODE; i++) {
        // vnpu_time_slice_sched_t unlink和munmap在shm_struct_cleanup里处理
        if (mgr->sched_structs[i] != NULL) {
            int vnpu_count = 0;
            if (mgr->shm_states[i] != NULL) {
                uint64_t bitmap0 = atomic_load(&mgr->shm_states[i]->vnpu_bitmap[0]);
                uint64_t bitmap1 = atomic_load(&mgr->shm_states[i]->vnpu_bitmap[1]);
                for (int v = 0; v < MAX_VNPU_PER_DIE; v++) {
                    int idx = v / 64;
                    int bit = v % 64;
                    uint64_t bitmap = (idx == 0) ? bitmap0 : bitmap1;
                    if ((bitmap >> bit) & 1)
                        vnpu_count++;
                }
            }
            shm_struct_cleanup(mgr->shm_states[i]->shm_id, mgr->sched_structs[i], vnpu_count);
            mgr->sched_structs[i] = NULL;
        }

        if (mgr->shm_states[i] != NULL) {
            destroy_shm_state(mgr->shm_states[i]);
            mgr->shm_states[i] = NULL;
        }
    }

    mgr->shm_state_count = 0;
    mgr->initialized = false;

    pthread_mutex_unlock(&mgr->lock);
    pthread_mutex_destroy(&mgr->lock);

    free(mgr);
    return ENPU_SUCCESS;
}

shm_state_t *shm_manager_get_state(shm_manager_t *mgr, int phy_id)
{
    if (mgr == NULL || phy_id < 0 || phy_id >= MAX_NPU_PER_NODE) {
        return NULL;
    }
    return mgr->shm_states[phy_id];
}

int shm_manager_get_or_create_shm(shm_manager_t *mgr, int phy_id, const char *die_id, char *shm_id_out,
                                  int shm_id_out_len)
{
    if (mgr == NULL || die_id == NULL) {
        return ENPU_INVALID_PARAM;
    }

    if (phy_id < 0 || phy_id >= MAX_NPU_PER_NODE) {
        return ENPU_INVALID_PARAM;
    }

    pthread_mutex_lock(&mgr->lock);

    shm_state_t *state = shm_manager_get_state(mgr, phy_id);
    if (state != NULL) {
        if (shm_id_out != NULL && shm_id_out_len > 0) {
            int ret = snprintf_s(shm_id_out, shm_id_out_len, shm_id_out_len - 1, "%s", state->shm_id);
            if (ret < 0) {
                LOG_ERROR("[SHM-MANAGER] snprintf_s shm_id_out failed, ret=%d", ret);
                pthread_mutex_unlock(&mgr->lock);
                return ENPU_FAIL;
            }
        }
        pthread_mutex_unlock(&mgr->lock);

        return ENPU_SUCCESS;
    }

    state = shm_state_posix_shm_create(phy_id, die_id);
    if (state == NULL) {
        pthread_mutex_unlock(&mgr->lock);
        return ENPU_FAIL;
    }

    mgr->shm_states[phy_id] = state;
    mgr->shm_state_count++;

    if (shm_id_out != NULL && shm_id_out_len > 0) {
        int ret = snprintf_s(shm_id_out, shm_id_out_len, shm_id_out_len - 1, "%s", state->shm_id);
        if (ret < 0) {
            LOG_ERROR("[SHM-MANAGER] snprintf_s shm_id_out failed, ret=%d", ret);
            destroy_shm_state(state);
            mgr->shm_states[phy_id] = NULL;
            mgr->shm_state_count--;
            pthread_mutex_unlock(&mgr->lock);
            return ENPU_FAIL;
        }
    }

    // attach每个vNPU的sched, AI CORE共享内存的shm_id就是die_id
    vnpu_time_slice_sched_t *sched = NULL;
    int ret = shm_struct_init(die_id, &sched);
    if (ret == ENPU_SUCCESS) {
        mgr->sched_structs[phy_id] = sched;
    } else {
        LOG_ERROR("[SHM-MANAGER] shm_struct_init failed, phy_id=%d, shm_id=%s, ret=%d", phy_id, die_id, ret);
    }

    atomic_store(&state->initialized, true);

    pthread_mutex_unlock(&mgr->lock);
    return ENPU_SUCCESS;
}

int shm_manager_register_vnpu(shm_manager_t *mgr, int phy_id, int vnpu_id, int aicore_quota, int sched_policy)
{
    if (mgr == NULL || phy_id < 0 || vnpu_id < 0) {
        return ENPU_INVALID_PARAM;
    }

    if (vnpu_id >= MAX_VNPU_PER_DIE) {
        return ENPU_INVALID_PARAM;
    }

    pthread_mutex_lock(&mgr->lock);

    shm_state_t *state = shm_manager_get_state(mgr, phy_id);
    if (state == NULL) {
        pthread_mutex_unlock(&mgr->lock);
        return ENPU_NOT_FOUND;
    }

    if (!atomic_load(&state->initialized)) {
        pthread_mutex_unlock(&mgr->lock);
        return ENPU_FAIL;
    }

    int idx = vnpu_id / 64;
    int bit = vnpu_id % 64;
    uint64_t bitmap = atomic_load(&state->vnpu_bitmap[idx]);

    if ((bitmap >> bit) & 1) {
        pthread_mutex_unlock(&mgr->lock);
        return ENPU_ALREADY_EXISTS;
    }

    if (state->sched_policy == -1) {
        state->sched_policy = sched_policy;
    } else if (state->sched_policy != sched_policy) {
        pthread_mutex_unlock(&mgr->lock);
        return ENPU_INVALID_PARAM;
    }

    vnpu_shm_entry_t new_entry = {
        .vnpu_id = vnpu_id,
        .hbm_request = 0,
        .hbm_limit = 0,
        .sched_policy = sched_policy,
        .swap_enabled = false,
    };
    atomic_store(&new_entry.hbm_used, 0);
    for (int p = 0; p < MAX_PROCESSES_PER_VNPU; p++) {
        atomic_store(&new_entry.processes[p].active, false);
        atomic_store(&new_entry.processes[p].swapped, false);
        atomic_store(&new_entry.processes[p].hbm_used, 0);
        atomic_store(&new_entry.processes[p].last_heartbeat_ns, 0);
        new_entry.processes[p].swap_offset = 0;
        new_entry.processes[p].swap_size = 0;
        atomic_store(&new_entry.processes[p].ack_pending_seq, 0);
    }

    state->entries[vnpu_id] = new_entry;
    atomic_fetch_or(&state->vnpu_bitmap[idx], (1ULL << bit));

    pthread_mutex_unlock(&mgr->lock);

    return ENPU_SUCCESS;
}

int shm_manager_unregister_vnpu(shm_manager_t *mgr, int phy_id, int vnpu_id)
{
    if (mgr == NULL || phy_id < 0 || vnpu_id < 0) {
        return ENPU_INVALID_PARAM;
    }

    pthread_mutex_lock(&mgr->lock);

    shm_state_t *state = shm_manager_get_state(mgr, phy_id);
    if (state == NULL) {
        pthread_mutex_unlock(&mgr->lock);
        return ENPU_NOT_FOUND;
    }

    int idx = vnpu_id / 64;
    int bit = vnpu_id % 64;
    uint64_t bitmap = atomic_load(&state->vnpu_bitmap[idx]);

    if (!((bitmap >> bit) & 1)) {
        pthread_mutex_unlock(&mgr->lock);
        return ENPU_NOT_FOUND;
    }

    state->entries[vnpu_id].vnpu_id = -1;
    atomic_fetch_and(&state->vnpu_bitmap[idx], ~(1ULL << bit));

    uint64_t bitmap0 = atomic_load(&state->vnpu_bitmap[0]);
    uint64_t bitmap1 = atomic_load(&state->vnpu_bitmap[1]);
    if (bitmap0 == 0 && bitmap1 == 0) {
        state->sched_policy = -1;
    }

    pthread_mutex_unlock(&mgr->lock);

    return ENPU_SUCCESS;
}

int shm_manager_add_entry(shm_manager_t *mgr, int phy_id, vnpu_shm_entry_t *entry)
{
    if (mgr == NULL || phy_id < 0 || entry == NULL) {
        return ENPU_INVALID_PARAM;
    }

    if (entry->vnpu_id < 0 || entry->vnpu_id >= MAX_VNPU_PER_DIE) {
        return ENPU_INVALID_PARAM;
    }

    pthread_mutex_lock(&mgr->lock);

    shm_state_t *state = shm_manager_get_state(mgr, phy_id);
    if (state == NULL) {
        pthread_mutex_unlock(&mgr->lock);
        return ENPU_NOT_FOUND;
    }

    int vnpu_id = entry->vnpu_id;
    int idx = vnpu_id / 64;
    int bit = vnpu_id % 64;
    uint64_t bitmap = atomic_load(&state->vnpu_bitmap[idx]);

    if ((bitmap >> bit) & 1) {
        pthread_mutex_unlock(&mgr->lock);
        return ENPU_ALREADY_EXISTS;
    }

    state->entries[vnpu_id] = *entry;
    atomic_fetch_or(&state->vnpu_bitmap[idx], (1ULL << bit));

    pthread_mutex_unlock(&mgr->lock);
    return ENPU_SUCCESS;
}

int shm_manager_remove_entry(shm_manager_t *mgr, int phy_id, int vnpu_id)
{
    if (mgr == NULL || phy_id < 0 || vnpu_id < 0) {
        return ENPU_INVALID_PARAM;
    }

    if (vnpu_id >= MAX_VNPU_PER_DIE) {
        return ENPU_INVALID_PARAM;
    }

    pthread_mutex_lock(&mgr->lock);

    shm_state_t *state = shm_manager_get_state(mgr, phy_id);
    if (state == NULL) {
        pthread_mutex_unlock(&mgr->lock);
        return ENPU_NOT_FOUND;
    }

    int idx = vnpu_id / 64;
    int bit = vnpu_id % 64;
    uint64_t bitmap = atomic_load(&state->vnpu_bitmap[idx]);

    if (!((bitmap >> bit) & 1)) {
        pthread_mutex_unlock(&mgr->lock);
        return ENPU_NOT_FOUND;
    }

    state->entries[vnpu_id].vnpu_id = -1;
    atomic_fetch_and(&state->vnpu_bitmap[idx], ~(1ULL << bit));

    pthread_mutex_unlock(&mgr->lock);
    return ENPU_SUCCESS;
}

int shm_manager_init_state(shm_manager_t *mgr, int phy_id, uint64_t hbm_total)
{
    if (mgr == NULL || phy_id < 0) {
        return ENPU_INVALID_PARAM;
    }

    pthread_mutex_lock(&mgr->lock);

    shm_state_t *state = shm_manager_get_state(mgr, phy_id);
    if (state == NULL) {
        pthread_mutex_unlock(&mgr->lock);
        return ENPU_NOT_FOUND;
    }

    state->hbm_total = hbm_total;
    atomic_store(&state->swap_cmd.completed, true);
    atomic_store(&state->swap_out_cmd.completed, true);

    pthread_mutex_unlock(&mgr->lock);
    return ENPU_SUCCESS;
}

static vnpu_shm_entry_t *find_entry(shm_state_t *state, int vnpu_id)
{
    if (state == NULL || vnpu_id < 0 || vnpu_id >= MAX_VNPU_PER_DIE) {
        return NULL;
    }

    int idx = vnpu_id / 64;
    int bit = vnpu_id % 64;
    uint64_t bitmap = atomic_load(&state->vnpu_bitmap[idx]);

    if ((bitmap >> bit) & 1) {
        return &state->entries[vnpu_id];
    }

    return NULL;
}

int shm_manager_set_swap_state(shm_manager_t *mgr, int phy_id, int vnpu_id, uint64_t offset, uint64_t size)
{
    if (mgr == NULL || phy_id < 0 || vnpu_id < 0) {
        return ENPU_INVALID_PARAM;
    }

    pthread_mutex_lock(&mgr->lock);

    shm_state_t *state = shm_manager_get_state(mgr, phy_id);
    if (state == NULL) {
        pthread_mutex_unlock(&mgr->lock);
        return ENPU_NOT_FOUND;
    }

    vnpu_shm_entry_t *entry = find_entry(state, vnpu_id);
    if (entry == NULL) {
        pthread_mutex_unlock(&mgr->lock);
        return ENPU_NOT_FOUND;
    }

    /* 进程粒度: swap 偏移/大小位于各 processes[slot] 上, vNpU 级不再维护聚合的 swap_offset/swap_size.
     * 调用方如需"清除该 vNpU 的 swap 状态", 使用 shm_free(state, vnpu_id).
     * 显式打印被忽略的入参, 避免调用方误以为 vNPU 级 swap 状态已被写入. */
    if (offset != 0 || size != 0) {
        LOG_WARN("[SHM-MANAGER] set_swap_state ignored offset=%lu size=%lu (vnpu_id=%d): swap state is per-process",
                 offset, size, vnpu_id);
    }

    pthread_mutex_unlock(&mgr->lock);

    return ENPU_SUCCESS;
}

int shm_manager_get_swap_state(shm_manager_t *mgr, int phy_id, int vnpu_id, bool *swapped, uint64_t *offset,
                               uint64_t *size)
{
    if (mgr == NULL || phy_id < 0 || vnpu_id < 0) {
        return ENPU_INVALID_PARAM;
    }

    if (swapped == NULL || offset == NULL || size == NULL) {
        return ENPU_INVALID_PARAM;
    }

    pthread_mutex_lock(&mgr->lock);

    shm_state_t *state = shm_manager_get_state(mgr, phy_id);
    if (state == NULL) {
        pthread_mutex_unlock(&mgr->lock);
        return ENPU_NOT_FOUND;
    }

    vnpu_shm_entry_t *entry = find_entry(state, vnpu_id);
    if (entry == NULL) {
        pthread_mutex_unlock(&mgr->lock);
        return ENPU_NOT_FOUND;
    }

    *swapped = true;
    *offset = 0;
    *size = 0;
    for (int slot = 0; slot < MAX_PROCESSES_PER_VNPU; slot++) {
        process_entry_t *pe = &entry->processes[slot];
        if (atomic_load(&pe->active) && !atomic_load(&pe->swapped)) {
            *swapped = false;
        }
        *size += pe->swap_size;
        if (*offset == 0 && pe->swap_size != 0) {
            *offset = pe->swap_offset;
        }
    }

    pthread_mutex_unlock(&mgr->lock);
    return ENPU_SUCCESS;
}

int shm_manager_write_swap_cmd(shm_manager_t *mgr, int phy_id, const char *pod_uid, int vnpu_id, int action,
                               uint8_t to_swap_out)
{
    if (mgr == NULL || phy_id < 0) {
        return ENPU_INVALID_PARAM;
    }

    if (pod_uid == NULL || vnpu_id < 0 || vnpu_id >= MAX_VNPU_PER_DIE) {
        return ENPU_INVALID_PARAM;
    }

    if (action < SWAP_ACTION_NONE || action > SWAP_ACTION_IN) {
        return ENPU_INVALID_PARAM;
    }

    pthread_mutex_lock(&mgr->lock);

    shm_state_t *state = shm_manager_get_state(mgr, phy_id);
    if (state == NULL) {
        pthread_mutex_unlock(&mgr->lock);
        return ENPU_NOT_FOUND;
    }

    int ret = snprintf_s(state->swap_cmd.target_pod_uid, sizeof(state->swap_cmd.target_pod_uid),
                         sizeof(state->swap_cmd.target_pod_uid) - 1, "%s", pod_uid);
    if (ret < 0) {
        LOG_ERROR("[SHM-MANAGER] snprintf_s target_pod_uid failed, ret=%d", ret);
        pthread_mutex_unlock(&mgr->lock);
        return ENPU_FAIL;
    }
    state->swap_cmd.target_vnpu_id = vnpu_id;
    if (to_swap_out != SWAP_OUT_NONE) {
        state->swap_out_cmd.target_vnpu_id = vnpu_id;
    }

    if (action == SWAP_ACTION_OUT) {
        vnpu_shm_entry_t *vnpu_entry = &state->entries[vnpu_id];
        /* 命令序号递增, 作为进程端去重与 ack 的 epoch */
        uint64_t seq = atomic_fetch_add(&state->swap_cmd.cmd_seq, 1) + 1;

        /* 先给每个活跃进程落下"欠当前 epoch 一次 ack"的标记, 再发布命令.
         * 标记只在真正 ack 时清零, 因此进程无论死在换出前/拷贝中/ack 前, 完成判定都不会丢失依据. */
        int count = 0;
        for (int i = 0; i < MAX_PROCESSES_PER_VNPU; i++) {
            process_entry_t *pe = &vnpu_entry->processes[i];
            if (atomic_load(&pe->active)) {
                atomic_store(&pe->ack_pending_seq, seq);
                count++;
            }
        }
        /* refcount 降级为快速路径计数(与 marker 扫描冗余), 不再单独承担完成判定 */
        atomic_store(&state->swap_cmd.refcount, count);

        /* completed 必须先于 action 发布: 否则最后一个进程可能在 completed=false 落盘前
         * 就完成 ack 并置 true, 随后被这里覆盖回 false, 导致命令永久不完成 */
        atomic_store(&state->swap_cmd.completed, count == 0);
    } else {
        atomic_store(&state->swap_cmd.completed, false);
    }

    state->swap_cmd.timestamp_ns = (uint64_t)time(NULL) * 1000000000ULL;
    atomic_store(&state->swap_cmd.action, action);

    pthread_mutex_unlock(&mgr->lock);
    return ENPU_SUCCESS;
}

int shm_manager_read_swap_cmd(shm_manager_t *mgr, int phy_id, char *pod_uid, int *vnpu_id, int *action, bool *completed)
{
    if (mgr == NULL || phy_id < 0) {
        return ENPU_INVALID_PARAM;
    }

    if (pod_uid == NULL || vnpu_id == NULL || action == NULL || completed == NULL) {
        return ENPU_INVALID_PARAM;
    }

    pthread_mutex_lock(&mgr->lock);

    shm_state_t *state = shm_manager_get_state(mgr, phy_id);
    if (state == NULL) {
        pthread_mutex_unlock(&mgr->lock);
        return ENPU_NOT_FOUND;
    }

    int ret = snprintf_s(pod_uid, MAX_UUID_LEN, MAX_UUID_LEN - 1, "%s", state->swap_cmd.target_pod_uid);
    if (ret < 0) {
        LOG_ERROR("[SHM-MANAGER] snprintf_s pod_uid failed, ret=%d", ret);
        pthread_mutex_unlock(&mgr->lock);
        return ENPU_FAIL;
    }
    *vnpu_id = state->swap_cmd.target_vnpu_id;
    *action = atomic_load(&state->swap_cmd.action);
    *completed = atomic_load(&state->swap_cmd.completed);

    pthread_mutex_unlock(&mgr->lock);
    return ENPU_SUCCESS;
}

/* 目标 vNPU 上是否还有槽位欠 seq 这次 ack (调用方需持有 mgr->lock) */
static bool swap_cmd_has_pending_ack_locked(shm_state_t *state, int vnpu_id, uint64_t seq)
{
    vnpu_shm_entry_t *entry = find_entry(state, vnpu_id);
    if (entry == NULL) {
        return false;
    }

    for (int i = 0; i < MAX_PROCESSES_PER_VNPU; i++) {
        if (atomic_load(&entry->processes[i].ack_pending_seq) == seq) {
            return true;
        }
    }
    return false;
}

/* 置换出命令完成: 与 vcann-rt 侧 shm_ack_swap_cmd 的行为保持一致,
 * 同时唤醒在 swap_out_cmd.completed 上忙等的 RT 进程 */
static void swap_cmd_mark_completed_locked(shm_state_t *state)
{
    atomic_store(&state->swap_cmd.completed, true);
    atomic_store(&state->swap_cmd.action, SWAP_ACTION_NONE);
    if (state->swap_cmd.target_vnpu_id == state->swap_out_cmd.target_vnpu_id) {
        atomic_store(&state->swap_out_cmd.completed, true);
    }
}

int shm_manager_ack_swap_cmd(shm_manager_t *mgr, int phy_id)
{
    if (mgr == NULL || phy_id < 0) {
        return ENPU_INVALID_PARAM;
    }

    pthread_mutex_lock(&mgr->lock);

    shm_state_t *state = shm_manager_get_state(mgr, phy_id);
    if (state == NULL) {
        pthread_mutex_unlock(&mgr->lock);
        return ENPU_NOT_FOUND;
    }

    swap_cmd_mark_completed_locked(state);

    pthread_mutex_unlock(&mgr->lock);
    return ENPU_SUCCESS;
}

int shm_manager_proxy_ack_process(shm_manager_t *mgr, int phy_id, int vnpu_id, int slot)
{
    if (mgr == NULL || phy_id < 0) {
        return ENPU_INVALID_PARAM;
    }

    if (vnpu_id < 0 || vnpu_id >= MAX_VNPU_PER_DIE || slot < 0 || slot >= MAX_PROCESSES_PER_VNPU) {
        return ENPU_INVALID_PARAM;
    }

    pthread_mutex_lock(&mgr->lock);

    shm_state_t *state = shm_manager_get_state(mgr, phy_id);
    if (state == NULL) {
        pthread_mutex_unlock(&mgr->lock);
        return ENPU_NOT_FOUND;
    }

    /* 只处理换出命令: 换入/无命令时槽位没有欠账, 不做任何改动 */
    if (atomic_load(&state->swap_cmd.action) != SWAP_ACTION_OUT) {
        pthread_mutex_unlock(&mgr->lock);
        return ENPU_SUCCESS;
    }

    uint64_t seq = atomic_load(&state->swap_cmd.cmd_seq);
    if (seq == 0) {
        pthread_mutex_unlock(&mgr->lock);
        return ENPU_SUCCESS;
    }

    process_entry_t *pe = &state->entries[vnpu_id].processes[slot];
    /* exchange 抢占: 只有"仍欠本 epoch"的槽位才会被代 ack, 已 ack 过的返回 0 不重复递减 */
    if (atomic_exchange(&pe->ack_pending_seq, 0) != seq) {
        pthread_mutex_unlock(&mgr->lock);
        return ENPU_SUCCESS;
    }

    int prev = atomic_fetch_sub(&state->swap_cmd.refcount, 1);
    int remain = (prev > 0) ? prev - 1 : 0;
    LOG_WARN("[POD-WATCHDOG] proxy ack for dead process: phy_id=%d vnpu_id=%d slot=%d vcann_rt_id=%d seq=%lu "
             "refcount %d->%d",
             phy_id, vnpu_id, slot, pe->vcann_rt_id, (unsigned long)seq, prev, remain);

    if (!swap_cmd_has_pending_ack_locked(state, vnpu_id, seq)) {
        swap_cmd_mark_completed_locked(state);
        LOG_WARN("[POD-WATCHDOG] swap_out completed after proxy ack: phy_id=%d vnpu_id=%d seq=%lu", phy_id, vnpu_id,
                 (unsigned long)seq);
    }

    pthread_mutex_unlock(&mgr->lock);
    return ENPU_SUCCESS;
}

int shm_manager_try_complete_swap_cmd(shm_manager_t *mgr, int phy_id)
{
    if (mgr == NULL || phy_id < 0) {
        return ENPU_INVALID_PARAM;
    }

    pthread_mutex_lock(&mgr->lock);

    shm_state_t *state = shm_manager_get_state(mgr, phy_id);
    if (state == NULL) {
        pthread_mutex_unlock(&mgr->lock);
        return ENPU_NOT_FOUND;
    }

    if (atomic_load(&state->swap_cmd.completed) || atomic_load(&state->swap_cmd.action) != SWAP_ACTION_OUT) {
        pthread_mutex_unlock(&mgr->lock);
        return ENPU_SUCCESS;
    }

    uint64_t seq = atomic_load(&state->swap_cmd.cmd_seq);
    int vnpu_id = state->swap_cmd.target_vnpu_id;
    if (seq == 0 || vnpu_id < 0 || vnpu_id >= MAX_VNPU_PER_DIE) {
        pthread_mutex_unlock(&mgr->lock);
        return ENPU_SUCCESS;
    }

    if (swap_cmd_has_pending_ack_locked(state, vnpu_id, seq)) {
        pthread_mutex_unlock(&mgr->lock);
        return ENPU_SUCCESS;
    }

    /* 已无任何槽位欠本 epoch 的 ack, 但仍未完成: 兜底置完成, 避免 die 级换出线程永久停摆 */
    LOG_WARN("[POD-WATCHDOG] swap_cmd completed by marker scan: phy_id=%d vnpu_id=%d seq=%lu refcount=%d", phy_id,
             vnpu_id, (unsigned long)seq, atomic_load(&state->swap_cmd.refcount));
    swap_cmd_mark_completed_locked(state);

    pthread_mutex_unlock(&mgr->lock);
    return ENPU_SUCCESS;
}

int shm_manager_get_vnpu_all_swapped(shm_manager_t *mgr, int phy_id, int vnpu_id, bool *swapped)
{
    if (mgr == NULL || phy_id < 0 || vnpu_id < 0) {
        return ENPU_INVALID_PARAM;
    }

    if (swapped == NULL) {
        return ENPU_INVALID_PARAM;
    }

    pthread_mutex_lock(&mgr->lock);

    shm_state_t *state = shm_manager_get_state(mgr, phy_id);
    if (state == NULL) {
        pthread_mutex_unlock(&mgr->lock);
        return ENPU_NOT_FOUND;
    }

    vnpu_shm_entry_t *entry = find_entry(state, vnpu_id);
    if (entry == NULL) {
        pthread_mutex_unlock(&mgr->lock);
        return ENPU_NOT_FOUND;
    }

    int active_count = 0;
    int swapped_count = 0;
    for (int slot = 0; slot < MAX_PROCESSES_PER_VNPU; slot++) {
        process_entry_t *pe = &entry->processes[slot];
        if (!atomic_load(&pe->active)) {
            continue;
        }
        active_count++;
        if (atomic_load(&pe->swapped)) {
            swapped_count++;
        }
    }

    /* 仅当存在活跃进程且全部已换出时才为 true; 空 vNPU 没有换出数据, 判 false */
    *swapped = (active_count > 0) && (swapped_count == active_count);

    /* 排查用: 区分"空 vNPU(无活跃进程)"与"真的全部换出" */
    LOG_DEBUG("[SHM] all_swapped check: phy_id=%d vnpu_id=%d active=%d swapped=%d all_swapped=%d%s", phy_id, vnpu_id,
              active_count, swapped_count, *swapped, active_count == 0 ? " (no active process)" : "");

    pthread_mutex_unlock(&mgr->lock);
    return ENPU_SUCCESS;
}

int shm_manager_get_vnpu_swapped(shm_manager_t *mgr, int phy_id, int vnpu_id, bool *swapped)
{
    return shm_manager_get_vnpu_all_swapped(mgr, phy_id, vnpu_id, swapped);
}

int shm_manager_set_swap_enabled(shm_manager_t *mgr, int phy_id, int vnpu_id, bool enabled)
{
    if (mgr == NULL || phy_id < 0 || vnpu_id < 0) {
        return ENPU_INVALID_PARAM;
    }

    pthread_mutex_lock(&mgr->lock);

    shm_state_t *state = shm_manager_get_state(mgr, phy_id);
    if (state == NULL) {
        pthread_mutex_unlock(&mgr->lock);
        return ENPU_NOT_FOUND;
    }

    vnpu_shm_entry_t *entry = find_entry(state, vnpu_id);
    if (entry == NULL) {
        pthread_mutex_unlock(&mgr->lock);
        return ENPU_NOT_FOUND;
    }

    entry->swap_enabled = enabled;

    pthread_mutex_unlock(&mgr->lock);

    return ENPU_SUCCESS;
}

int shm_manager_get_swap_enabled(shm_manager_t *mgr, int phy_id, int vnpu_id, bool *enabled)
{
    if (mgr == NULL || phy_id < 0 || vnpu_id < 0) {
        return ENPU_INVALID_PARAM;
    }

    if (enabled == NULL) {
        return ENPU_INVALID_PARAM;
    }

    pthread_mutex_lock(&mgr->lock);

    shm_state_t *state = shm_manager_get_state(mgr, phy_id);
    if (state == NULL) {
        pthread_mutex_unlock(&mgr->lock);
        return ENPU_NOT_FOUND;
    }

    vnpu_shm_entry_t *entry = find_entry(state, vnpu_id);
    if (entry == NULL) {
        pthread_mutex_unlock(&mgr->lock);
        return ENPU_NOT_FOUND;
    }

    *enabled = entry->swap_enabled;

    pthread_mutex_unlock(&mgr->lock);
    return ENPU_SUCCESS;
}

shm_state_t *shm_state_posix_shm_create(int phy_id, const char *die_id)
{
    if (phy_id < 0 || die_id == NULL) {
        return NULL;
    }

    char shm_name[128];
    int ret = snprintf_s(shm_name, sizeof(shm_name), sizeof(shm_name) - 1, "/shm_state-%d-%s", phy_id, die_id);
    if (ret < 0) {
        LOG_ERROR("[SHM-MANAGER] snprintf_s shm_name failed, ret=%d", ret);
        return NULL;
    }

    /* Never truncate or reinitialize an existing object's data and mutex. */
    int fd = shm_open(shm_name, O_CREAT | O_EXCL | O_RDWR, 0644);
    if (fd < 0) {
        LOG_ERROR("[SHM-MANAGER] create shm state failed, shm_id=%s, errno=%d", shm_name, errno);
        return NULL;
    }

    size_t size = sizeof(shm_state_t);
    if (ftruncate(fd, size) == -1) {
        close(fd);
        shm_unlink(shm_name);
        return NULL;
    }

    void *addr = mmap(NULL, size, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
    close(fd);

    if (addr == MAP_FAILED) {
        shm_unlink(shm_name);
        return NULL;
    }

    shm_state_t *state = (shm_state_t *)addr;
    (void)memset_s(state, size, 0, size);

    state->version = SHM_STATE_VERSION;
    state->phy_id = phy_id;
    ret = strncpy_s(state->shm_id, DIE_ID_LEN, die_id, strnlen(die_id, DIE_ID_LEN - 1));
    if (ret != 0) {
        LOG_ERROR("[SHM-MANAGER] strncpy_s shm_id failed, ret=%d", ret);
        munmap(state, size);
        shm_unlink(shm_name);
        return NULL;
    }
    state->hbm_total = 0;
    atomic_store(&state->vnpu_bitmap[0], 0);
    atomic_store(&state->vnpu_bitmap[1], 0);
    state->sched_policy = -1;

    pthread_mutexattr_t attr;
    pthread_mutexattr_init(&attr);
    pthread_mutexattr_setpshared(&attr, PTHREAD_PROCESS_SHARED);
    pthread_mutexattr_setrobust(&attr, PTHREAD_MUTEX_ROBUST);
    pthread_mutex_init(&state->swap_in_mutex, &attr);
    pthread_mutexattr_destroy(&attr);

    atomic_store(&state->initialized, false);
    (void)memset_s(state->reserved, sizeof(state->reserved), 0, sizeof(state->reserved));

    /* 新增: 初始化 vcann_rt_id 计数器 (ID 从 1 开始, 0 表示未分配) */
    atomic_store(&state->next_vcann_rt_id, 1);

    /* 新增: 初始化每个 vNPU 的进程注册表 */
    for (int v = 0; v < MAX_VNPU_PER_DIE; v++) {
        for (int p = 0; p < MAX_PROCESSES_PER_VNPU; p++) {
            atomic_store(&state->entries[v].processes[p].active, false);
            atomic_store(&state->entries[v].processes[p].swapped, false);
            atomic_store(&state->entries[v].processes[p].hbm_used, 0);
            atomic_store(&state->entries[v].processes[p].last_heartbeat_ns, 0);
            state->entries[v].processes[p].swap_offset = 0;
            state->entries[v].processes[p].swap_size = 0;
            atomic_store(&state->entries[v].processes[p].ack_pending_seq, 0);
        }
    }

    /* 新增: 初始化 swap_cmd.refcount / cmd_seq */
    atomic_store(&state->swap_cmd.refcount, 0);
    atomic_store(&state->swap_cmd.cmd_seq, 0);

    return state;
}

shm_state_t *shm_state_posix_shm_attach(int phy_id, const char *die_id)
{
    if (phy_id < 0 || die_id == NULL) {
        return NULL;
    }

    char shm_name[128];
    int ret = snprintf_s(shm_name, sizeof(shm_name), sizeof(shm_name) - 1, "/shm_state-%d-%s", phy_id, die_id);
    if (ret < 0) {
        LOG_ERROR("[SHM-MANAGER] snprintf_s shm_name failed, ret=%d", ret);
        return NULL;
    }

    int fd = shm_open(shm_name, O_RDWR, 0644);
    if (fd < 0) {
        return NULL;
    }

    size_t size = sizeof(shm_state_t);

    struct stat st;
    if (fstat(fd, &st) != 0 || (uint64_t)st.st_size != size) {
        close(fd);
        return NULL;
    }

    void *addr = mmap(NULL, size, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
    close(fd);

    if (addr == MAP_FAILED) {
        return NULL;
    }

    shm_state_t *state = (shm_state_t *)addr;
    if (state->version != SHM_STATE_VERSION) {
        LOG_ERROR("[SHM-MANAGER] shm state version mismatch, shm_id=%s, actual=%u, expected=%u", shm_name,
                  state->version, SHM_STATE_VERSION);
        munmap(state, size);
        return NULL;
    }
    return state;
}

int shm_state_posix_shm_destroy(int phy_id, const char *die_id)
{
    if (phy_id < 0 || die_id == NULL) {
        return ENPU_INVALID_PARAM;
    }

    char shm_name[128];
    int ret = snprintf_s(shm_name, sizeof(shm_name), sizeof(shm_name) - 1, "/shm_state-%d-%s", phy_id, die_id);
    if (ret < 0) {
        LOG_ERROR("[SHM-MANAGER] snprintf_s shm_name failed, ret=%d", ret);
        return ENPU_FAIL;
    }

    shm_unlink(shm_name);
    return ENPU_SUCCESS;
}

// TLSF 块头
typedef struct TLSFBlockHeader {
    size_t size;
    struct TLSFBlockHeader *next_free;
    struct TLSFBlockHeader *prev_free;
    struct TLSFBlockHeader *prev_phys; // 物理前一块
    struct TLSFBlockHeader *next_phys; // 物理后一块
    uint64_t data_offset;
} TLSFBlockHeader;

// TLSF 管理器
typedef struct {
    pthread_mutex_t tlsf_lock;
    bool initialized;
    TLSFBlockHeader *free_list[TLSF_MAX_FL][TLSF_MAX_SL];
    TLSFBlockHeader *used_list; // 全局已使用块链表（关键修复）
    uint64_t shm_base;
    size_t shm_total_size;
    uint64_t total_used;
} TLSFManager;

static TLSFManager g_tlsf_mgr;

static inline size_t block_get_size(TLSFBlockHeader *block)
{
    return block->size & ~1UL;
}

static inline bool block_is_used(TLSFBlockHeader *block)
{
    return (block->size & 1UL) != 0;
}

static inline void block_set_size(TLSFBlockHeader *block, size_t size)
{
    block->size = size | (block->size & 1UL);
}

static inline void block_mark_as_free(TLSFBlockHeader *block)
{
    block->size &= ~1UL;
}

static inline void block_mark_as_used(TLSFBlockHeader *block)
{
    block->size |= 1UL;
}

static void tlsf_calc_fl_sl(size_t size, size_t *fl, size_t *sl)
{
    size_t align_sz = (size + TLSF_ALIGN - 1) & ~(TLSF_ALIGN - 1);
    if (align_sz < TLSF_ALIGN)
        align_sz = TLSF_ALIGN;

    if (align_sz < 256) {
        *fl = 0;
        *sl = (align_sz - 1) / 16;
        return;
    }

    int log2_val = 63 - __builtin_clzll((unsigned long long)align_sz);
    *fl = (size_t)log2_val;
    *sl = (align_sz >> (*fl - 4)) & (TLSF_MAX_SL - 1);

    if (*fl >= TLSF_MAX_FL)
        *fl = TLSF_MAX_FL - 1;
    if (*sl >= TLSF_MAX_SL)
        *sl = TLSF_MAX_SL - 1;
}

static void tlsf_insert_free_block(TLSFManager *mgr, TLSFBlockHeader *block)
{
    size_t fl, sl;
    tlsf_calc_fl_sl(block_get_size(block), &fl, &sl);
    block->prev_free = NULL;
    block->next_free = mgr->free_list[fl][sl];
    if (mgr->free_list[fl][sl]) {
        mgr->free_list[fl][sl]->prev_free = block;
    }
    mgr->free_list[fl][sl] = block;
}

static void tlsf_remove_free_block(TLSFManager *mgr, TLSFBlockHeader *block)
{
    size_t fl, sl;
    tlsf_calc_fl_sl(block_get_size(block), &fl, &sl);

    if (block->prev_free) {
        block->prev_free->next_free = block->next_free;
    } else {
        mgr->free_list[fl][sl] = block->next_free;
    }
    if (block->next_free) {
        block->next_free->prev_free = block->prev_free;
    }
    block->prev_free = NULL;
    block->next_free = NULL;
}

static void tlsf_insert_used_block(TLSFManager *mgr, TLSFBlockHeader *block)
{
    block->next_free = mgr->used_list;
    mgr->used_list = block;
}

static void tlsf_remove_used_block(TLSFManager *mgr, TLSFBlockHeader *block)
{
    TLSFBlockHeader **p = &mgr->used_list;
    while (*p) {
        if (*p == block) {
            *p = block->next_free;
            block->next_free = NULL;
            return;
        }
        p = &(*p)->next_free;
    }
}

static TLSFBlockHeader *tlsf_find_block_by_offset(TLSFManager *mgr, uint64_t offset)
{
    // 先查已使用链表
    TLSFBlockHeader *cur = mgr->used_list;
    while (cur) {
        if (cur->data_offset == offset) {
            return cur;
        }
        cur = cur->next_free;
    }
    // 再查空闲链表
    for (size_t i = 0; i < TLSF_MAX_FL; i++) {
        for (size_t j = 0; j < TLSF_MAX_SL; j++) {
            cur = mgr->free_list[i][j];
            while (cur) {
                if (cur->data_offset == offset) {
                    return cur;
                }
                cur = cur->next_free;
            }
        }
    }
    return NULL;
}

static TLSFBlockHeader *tlsf_find_block(TLSFManager *mgr, size_t size)
{
    size_t fl, sl;
    tlsf_calc_fl_sl(size, &fl, &sl);

    for (size_t i = fl; i < TLSF_MAX_FL; i++) {
        for (size_t j = 0; j < TLSF_MAX_SL; j++) {
            TLSFBlockHeader *blk = mgr->free_list[i][j];
            while (blk != NULL) {
                if (block_get_size(blk) >= size) {
                    return blk;
                }
                blk = blk->next_free;
            }
        }
    }
    return NULL;
}

static TLSFBlockHeader *tlsf_split_block(TLSFManager *mgr, TLSFBlockHeader *blk, size_t need)
{
    size_t remain = block_get_size(blk) - need;
    if (remain < TLSF_ALIGN) {
        return blk;
    }

    uint64_t new_off = blk->data_offset + need;
    TLSFBlockHeader *new_blk = (TLSFBlockHeader *)malloc(sizeof(TLSFBlockHeader));
    if (!new_blk) {
        return blk;
    }

    block_set_size(new_blk, remain);
    block_mark_as_free(new_blk);
    new_blk->data_offset = new_off;

    // 维护物理双向链表
    new_blk->prev_phys = blk;
    new_blk->next_phys = blk->next_phys;
    if (blk->next_phys) {
        blk->next_phys->prev_phys = new_blk;
    }
    blk->next_phys = new_blk;

    new_blk->prev_free = NULL;
    new_blk->next_free = NULL;

    block_set_size(blk, need);
    tlsf_insert_free_block(mgr, new_blk);
    return blk;
}

static int tlsf_pool_init(uint64_t shm_base, size_t shm_size)
{
    TLSFManager *mgr = &g_tlsf_mgr;
    if (mgr->initialized) {
        return 0;
    }

    /* Only enpu-manager allocates/frees; TLSF metadata remains process-local.
     * Other processes only read/write the shared payload. This mutex protects allocator threads;
     * PTHREAD_PROCESS_SHARED does not enable allocation from other processes. */
    pthread_mutexattr_t attr;
    pthread_mutexattr_init(&attr);
    pthread_mutexattr_setpshared(&attr, PTHREAD_PROCESS_SHARED);
    pthread_mutex_init(&mgr->tlsf_lock, &attr);
    pthread_mutexattr_destroy(&attr);

    int rc = pthread_mutex_lock(&mgr->tlsf_lock);
    if (rc != 0)
        return -1;

    (void)memset_s(mgr->free_list, sizeof(mgr->free_list), 0, sizeof(mgr->free_list));
    mgr->used_list = NULL;
    mgr->shm_base = shm_base;
    mgr->shm_total_size = shm_size;
    mgr->total_used = 0;

    TLSFBlockHeader *start_blk = (TLSFBlockHeader *)malloc(sizeof(TLSFBlockHeader));
    if (!start_blk) {
        pthread_mutex_unlock(&mgr->tlsf_lock);
        return -1;
    }
    block_set_size(start_blk, shm_size);
    block_mark_as_free(start_blk);
    start_blk->data_offset = 0;
    start_blk->prev_phys = NULL;
    start_blk->next_phys = NULL;
    start_blk->prev_free = NULL;
    start_blk->next_free = NULL;

    tlsf_insert_free_block(mgr, start_blk);
    mgr->initialized = true;
    pthread_mutex_unlock(&mgr->tlsf_lock);
    return 0;
}

static uint64_t tlsf_alloc(size_t size)
{
    TLSFManager *mgr = &g_tlsf_mgr;
    if (!mgr->initialized || size == 0) {
        return (uint64_t)-1;
    }

    int rc = pthread_mutex_lock(&mgr->tlsf_lock);
    if (rc != 0)
        return (uint64_t)-1;

    size_t alloc_sz = (size + TLSF_ALIGN - 1) & ~(TLSF_ALIGN - 1);
    TLSFBlockHeader *blk = tlsf_find_block(mgr, alloc_sz);
    if (!blk) {
        pthread_mutex_unlock(&mgr->tlsf_lock);
        return (uint64_t)-1;
    }

    tlsf_remove_free_block(mgr, blk);
    blk = tlsf_split_block(mgr, blk, alloc_sz);
    block_mark_as_used(blk);
    tlsf_insert_used_block(mgr, blk);
    mgr->total_used += block_get_size(blk);

    uint64_t ret_off = blk->data_offset;
    pthread_mutex_unlock(&mgr->tlsf_lock);
    return ret_off;
}

static void tlsf_free(uint64_t offset)
{
    TLSFManager *mgr = &g_tlsf_mgr;
    if (!mgr->initialized || offset >= mgr->shm_total_size) {
        return;
    }

    int rc = pthread_mutex_lock(&mgr->tlsf_lock);
    if (rc != 0)
        return;

    TLSFBlockHeader *blk = tlsf_find_block_by_offset(mgr, offset);
    if (!blk || !block_is_used(blk)) {
        pthread_mutex_unlock(&mgr->tlsf_lock);
        return;
    }

    // 从已使用链表摘除
    tlsf_remove_used_block(mgr, blk);
    mgr->total_used -= block_get_size(blk);
    block_mark_as_free(blk);

    // 向前合并
    if (blk->prev_phys && !block_is_used(blk->prev_phys)) {
        TLSFBlockHeader *prev = blk->prev_phys;
        tlsf_remove_free_block(mgr, prev);
        size_t total = block_get_size(prev) + block_get_size(blk);
        block_set_size(prev, total);

        prev->next_phys = blk->next_phys;
        if (blk->next_phys) {
            blk->next_phys->prev_phys = prev;
        }
        free(blk);
        blk = prev;
    }

    // 向后合并
    if (blk->next_phys && !block_is_used(blk->next_phys)) {
        TLSFBlockHeader *next = blk->next_phys;
        tlsf_remove_free_block(mgr, next);
        size_t total = block_get_size(blk) + block_get_size(next);
        block_set_size(blk, total);

        blk->next_phys = next->next_phys;
        if (next->next_phys) {
            next->next_phys->prev_phys = blk;
        }
        free(next);
    }

    tlsf_insert_free_block(mgr, blk);
    pthread_mutex_unlock(&mgr->tlsf_lock);
}

swap_buffer_manager_t *shm_swap_buffer_init(uint64_t buffer_size)
{
    if (buffer_size == 0) {
        return NULL;
    }

    swap_buffer_manager_t *mgr = (swap_buffer_manager_t *)calloc(1, sizeof(swap_buffer_manager_t));
    if (mgr == NULL) {
        return NULL;
    }

    mgr->buffer_size = buffer_size;
    mgr->swap_buffer_base = NULL;

    if (pthread_mutex_init(&mgr->lock, NULL) != 0) {
        free(mgr);
        return NULL;
    }

    char shm_name[128] = {0};
    int ret = snprintf_s(shm_name, sizeof(shm_name), sizeof(shm_name) - 1, SHM_SWAP_BUFFER_NAME);
    if (ret < 0) {
        LOG_ERROR("[SHM-MANAGER] snprintf_s shm_name failed, ret=%d", ret);
        pthread_mutex_destroy(&mgr->lock);
        free(mgr);
        return NULL;
    }

    int fd = shm_open(shm_name, O_CREAT | O_RDWR, 0644);
    if (fd < 0) {
        pthread_mutex_destroy(&mgr->lock);
        free(mgr);
        return NULL;
    }

    struct stat st;
    if (fstat(fd, &st) < 0) {
        close(fd);
        shm_unlink(shm_name);
        pthread_mutex_destroy(&mgr->lock);
        free(mgr);
        return NULL;
    }

    if (st.st_size == 0) {
        if (ftruncate(fd, buffer_size) < 0) {
            close(fd);
            shm_unlink(shm_name);
            pthread_mutex_destroy(&mgr->lock);
            free(mgr);
            return NULL;
        }
    } else if ((uint64_t)st.st_size != buffer_size) {
        LOG_ERROR("[SHM-MANAGER] swap buffer size mismatch, shm_id=%s, actual=%lld, expected=%llu", shm_name,
                  (long long)st.st_size, (unsigned long long)buffer_size);
        close(fd);
        pthread_mutex_destroy(&mgr->lock);
        free(mgr);
        return NULL;
    }

    void *base = mmap(NULL, buffer_size, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
    close(fd);
    if (base == MAP_FAILED) {
        shm_unlink(shm_name);
        pthread_mutex_destroy(&mgr->lock);
        free(mgr);
        return NULL;
    }
    mgr->swap_buffer_base = base;

    // 初始化 TLSF
    if (tlsf_pool_init((uint64_t)base, buffer_size) != 0) {
        munmap(base, buffer_size);
        shm_unlink(shm_name);
        pthread_mutex_destroy(&mgr->lock);
        free(mgr);
        return NULL;
    }
    shm_print_free_block_info();

    return mgr;
}

int shm_swap_buffer_deinit(swap_buffer_manager_t *mgr)
{
    if (mgr == NULL) {
        return ENPU_INVALID_PARAM;
    }

    uint64_t buf_size = mgr->buffer_size;

    pthread_mutex_destroy(&mgr->lock);
    free(mgr);

    // 共享内存 + TLSF 释放逻辑
    TLSFManager *tls_mgr = &g_tlsf_mgr;
    if (tls_mgr->initialized) {
        pthread_mutex_lock(&tls_mgr->tlsf_lock);

        // 释放 TLSF 已使用块头
        TLSFBlockHeader *cur = tls_mgr->used_list;
        while (cur) {
            TLSFBlockHeader *tmp = cur;
            cur = cur->next_free;
            free(tmp);
        }

        // 释放 TLSF 空闲块头
        for (size_t i = 0; i < TLSF_MAX_FL; i++) {
            for (size_t j = 0; j < TLSF_MAX_SL; j++) {
                cur = tls_mgr->free_list[i][j];
                while (cur) {
                    TLSFBlockHeader *tmp = cur;
                    cur = cur->next_free;
                    free(tmp);
                }
            }
        }
        pthread_mutex_unlock(&tls_mgr->tlsf_lock);

        // 解除共享内存映射
        if (tls_mgr->shm_base != 0) {
            munmap((void *)tls_mgr->shm_base, buf_size);
        }

        // 删除共享内存文件
        char shm_name[128] = {0};
        int ret = snprintf_s(shm_name, sizeof(shm_name), sizeof(shm_name) - 1, SHM_SWAP_BUFFER_NAME);
        if (ret < 0) {
            LOG_ERROR("[SHM-MANAGER] snprintf_s shm_name failed, ret=%d", ret);
        } else {
            shm_unlink(shm_name);
        }

        // 销毁锁、重置 TLSF 管理器
        pthread_mutex_destroy(&tls_mgr->tlsf_lock);
        (void)memset_s(tls_mgr, sizeof(TLSFManager), 0, sizeof(TLSFManager));
        if (ret < 0) {
            return ENPU_FAIL;
        }
    }

    return ENPU_SUCCESS;
}

uint64_t shm_swap_buffer_alloc_offset(shm_state_t *state, uint64_t size)
{
    (void)state;
    return tlsf_alloc(size);
}

void shm_swap_buffer_free_offset(shm_state_t *state, uint64_t offset, uint64_t size)
{
    (void)state;
    (void)size;
    tlsf_free(offset);
}

int shm_swap_buffer_get_status(swap_buffer_status_t *status)
{
    if (status == NULL) {
        return ENPU_INVALID_PARAM;
    }
    TLSFManager *mgr = &g_tlsf_mgr;
    if (!mgr->initialized) {
        return ENPU_FAIL;
    }
    pthread_mutex_lock(&mgr->tlsf_lock);
    status->total = (uint64_t)mgr->shm_total_size;
    status->used = mgr->total_used;
    status->free = (status->total > status->used) ? (status->total - status->used) : 0;
    pthread_mutex_unlock(&mgr->tlsf_lock);
    return ENPU_SUCCESS;
}

int shm_alloc_per_process(shm_state_t *state, int vnpu_id, int slot, uint64_t size)
{
    if (!state || vnpu_id < 0 || vnpu_id >= MAX_VNPU_PER_DIE || slot < 0 || slot >= MAX_PROCESSES_PER_VNPU)
        return -1;

    process_entry_t *pe = &state->entries[vnpu_id].processes[slot];
    if (!atomic_load(&pe->active)) {
        return -1;
    }

    uint64_t off = shm_swap_buffer_alloc_offset(state, size);
    if (off == (uint64_t)-1)
        return -1;

    pe->swap_offset = off;
    pe->swap_size = size;
    shm_print_free_block_info();
    return 0;
}

int shm_free_per_process(shm_state_t *state, int vnpu_id, int slot)
{
    if (!state || vnpu_id < 0 || vnpu_id >= MAX_VNPU_PER_DIE || slot < 0 || slot >= MAX_PROCESSES_PER_VNPU)
        return -1;

    process_entry_t *pe = &state->entries[vnpu_id].processes[slot];
    if (pe->swap_size == 0) {
        return 0;
    }

    shm_swap_buffer_free_offset(state, pe->swap_offset, pe->swap_size);
    pe->swap_offset = 0;
    pe->swap_size = 0;
    shm_print_free_block_info();
    return 0;
}

int shm_alloc(shm_state_t *state, int vnpu_id, uint64_t size)
{
    (void)size;
    if (!state || vnpu_id < 0 || vnpu_id >= MAX_VNPU_PER_DIE)
        return -1;

    /* 进程粒度: 为每个活跃未换出的进程分配其 hbm_used 大小的 swap buffer.
     * 由调用方 (swap_resolver_write_swap_command) 逐进程分配. */
    LOG_WARN("shm_alloc: vNpU 级分配已废弃, 请改用 shm_alloc_per_process (vnpu=%d)", vnpu_id);
    return -1;
}

int shm_free(shm_state_t *state, int vnpu_id)
{
    if (!state || vnpu_id < 0 || vnpu_id >= MAX_VNPU_PER_DIE)
        return -1;

    /* 进程粒度: 释放该 vNPU 下所有进程的 swap buffer */
    vnpu_shm_entry_t *entry = &state->entries[vnpu_id];
    for (int slot = 0; slot < MAX_PROCESSES_PER_VNPU; slot++) {
        (void)shm_free_per_process(state, vnpu_id, slot);
    }
    return 0;
}

void shm_print_free_block_info(void)
{
    TLSFManager *mgr = &g_tlsf_mgr;
    pthread_mutex_lock(&mgr->tlsf_lock);

    LOG_INFO("==================== Current Free Block List ====================");

    uint32_t count = 0;
    for (size_t i = 0; i < TLSF_MAX_FL; i++) {
        for (size_t j = 0; j < TLSF_MAX_SL; j++) {
            TLSFBlockHeader *blk = mgr->free_list[i][j];
            while (blk != NULL) {
                count++;
                size_t blk_size = block_get_size(blk);
                double mb_size = (double)blk_size / (1024 * 1024);
                LOG_INFO("[Free Block %u] offset: 0x%lx  size: %lu bytes  (%.2f MB)", count,
                         (unsigned long)blk->data_offset, blk_size, mb_size);
                blk = blk->next_free;
            }
        }
    }

    if (count == 0) {
        LOG_INFO("No free blocks available");
    }
    LOG_INFO("====================================================================");

    pthread_mutex_unlock(&mgr->tlsf_lock);
}
