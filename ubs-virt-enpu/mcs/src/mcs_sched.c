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

#include "mcs_sched.h"
#include <errno.h>
#include <fcntl.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>

static uint64_t mcs_ns_now(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint64_t)ts.tv_sec * 1000000000ULL + (uint64_t)ts.tv_nsec;
}

static void mcs_ns_sleep(uint64_t ns)
{
    struct timespec req;
    struct timespec rem;
    req.tv_sec = 0;
    req.tv_nsec = (long)ns;
    while (nanosleep(&req, &rem) == -1) {
        if (errno == EINTR) {
            req = rem;
        } else {
            break;
        }
    }
}

static void mcs_shared_state_init(mcs_shared_state_t *state)
{
    (void)memset(state, 0, sizeof(*state));
    pthread_mutexattr_t attr;
    pthread_mutexattr_init(&attr);
    pthread_mutexattr_setpshared(&attr, PTHREAD_PROCESS_SHARED);
    pthread_mutex_init(&state->mcs_mutex, &attr);
    pthread_mutexattr_destroy(&attr);
    /* magic_number is published by the caller with release semantics */
}

int mcs_shm_init(const char *shm_id, mcs_shared_state_t **out_state)
{
    if (shm_id == NULL || out_state == NULL) {
        return -1;
    }

    int fd = shm_open(shm_id, O_CREAT | O_RDWR, S_IWUSR | S_IRUSR);
    if (fd == -1) {
        return -1;
    }

    struct stat st;
    if (fstat(fd, &st) != 0) {
        close(fd);
        return -1;
    }
    if (st.st_size >= 0 && (size_t)st.st_size < sizeof(mcs_shared_state_t)) {
        if (ftruncate(fd, (off_t)sizeof(mcs_shared_state_t)) == -1) {
            close(fd);
            return -1;
        }
    }

    void *addr = mmap(NULL, sizeof(mcs_shared_state_t), PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
    close(fd);

    if (addr == MAP_FAILED) {
        return -1;
    }

    mcs_shared_state_t *state = (mcs_shared_state_t *)addr;

    /* Claim first-init rights with an atomic CAS on magic_number. The mutex
     * cannot be used here: on a fresh shm page it is still all-zero, i.e. a
     * never-initialized process-private mutex — locking it cross-process
     * deadlocks (the private futex is never woken), and a racy re-init would
     * memset state that other processes are already using. The winner
     * initializes, then publishes with a release store; losers spin on magic. */
    uint32_t observed = __atomic_load_n(&state->magic_number, __ATOMIC_ACQUIRE);
    while (observed != MCS_MAGIC_INITIALIZED) {
        if (observed != MCS_MAGIC_INITIALIZING) {
            uint32_t expected = observed;
            if (__atomic_compare_exchange_n(&state->magic_number, &expected, MCS_MAGIC_INITIALIZING, false,
                                            __ATOMIC_ACQ_REL, __ATOMIC_ACQUIRE)) {
                mcs_shared_state_init(state);
                __atomic_store_n(&state->magic_number, MCS_MAGIC_INITIALIZED, __ATOMIC_RELEASE);
                break;
            }
            observed = expected;
        } else {
            mcs_ns_sleep(MCS_WAIT_SLEEP_NS);
            observed = __atomic_load_n(&state->magic_number, __ATOMIC_ACQUIRE);
        }
    }

    *out_state = state;
    return 0;
}

void mcs_shm_cleanup(const char *shm_id, mcs_shared_state_t *state)
{
    if (state != NULL) {
        (void)munmap(state, sizeof(mcs_shared_state_t));
    }
    if (shm_id != NULL) {
        (void)shm_unlink(shm_id);
    }
}

void mcs_mark_active(mcs_shared_state_t *state, int vnpu_id)
{
    if (state == NULL || vnpu_id < 0 || vnpu_id >= MCS_MAX_VNPU) {
        return;
    }
    state->last_kernel_time_ns[vnpu_id] = mcs_ns_now();
}

int mcs_has_high_priority_active(mcs_shared_state_t *state, int my_vnpu_id)
{
    if (state == NULL || my_vnpu_id < 0 || my_vnpu_id >= MCS_MAX_VNPU) {
        return 0;
    }

    uint64_t now = mcs_ns_now();
    uint8_t my_priority = state->vnpu_priority[my_vnpu_id];

    /* Only normal priority (0) tasks wait; high priority (1) tasks never wait */
    if (my_priority != 0) {
        return 0;
    }

    /* Check if any high priority (1) vNPU has recent activity */
    for (int i = 0; i < MCS_MAX_VNPU; i++) {
        if (i == my_vnpu_id) {
            continue;
        }
        if (state->vnpu_priority[i] != 1) {
            continue;
        }
        uint64_t last = state->last_kernel_time_ns[i];
        if (last == 0) {
            continue;
        }
        if (now >= last && (now - last) <= MCS_NO_TASK_TIMEOUT_NS) {
            return 1;
        }
        /* Handle timer wrap */
        if (now < last && (last - now) <= MCS_NO_TASK_TIMEOUT_NS) {
            return 1;
        }
    }
    return 0;
}

void mcs_wait_for_idle(mcs_shared_state_t *state, int my_vnpu_id)
{
    if (state == NULL) {
        return;
    }
    while (mcs_has_high_priority_active(state, my_vnpu_id)) {
        mcs_ns_sleep(MCS_WAIT_SLEEP_NS);
    }
}

/* Anti-starvation burst limit: how many high-priority kernel launches
 * entitle one normal-priority launch while a high-priority vNPU keeps the
 * device busy. Tunable via MCS_STARVATION_LIMIT (>= 1); default 300. Read
 * once per process. */
static uint64_t mcs_high_launch_burst_limit(void)
{
    static int64_t limit = -1;
    if (limit < 0) {
        const char *env = getenv("MCS_STARVATION_LIMIT");
        long v = MCS_HIGH_LAUNCH_BURST_DEFAULT;
        if (env != NULL && env[0] != '\0') {
            char *end = NULL;
            long parsed = strtol(env, &end, 10);
            if (end != env && *end == '\0' && parsed >= 1) {
                v = parsed;
            }
        }
        limit = v;
    }
    return (uint64_t)limit;
}

void mcs_before_launch(mcs_shared_state_t *state, int vnpu_id, uint8_t my_priority)
{
    if (state == NULL || vnpu_id < 0 || vnpu_id >= MCS_MAX_VNPU) {
        return;
    }

    if (my_priority == 1) {
        /* High priority: never waits; every launch feeds the burst counter
         * that eventually entitles a normal-priority launch. */
        (void)__atomic_fetch_add(&state->high_launch_count, 1ULL, __ATOMIC_RELAXED);
        state->last_kernel_time_ns[vnpu_id] = mcs_ns_now();
        return;
    }

    /* Normal priority: wait only while a high-priority vNPU is active
     * (5ms window) AND its burst count has not reached the limit. The 5ms
     * idle rule is unchanged; the burst limit prevents starvation when a
     * high-priority task launches continuously. */
    const uint64_t limit = mcs_high_launch_burst_limit();
    while (mcs_has_high_priority_active(state, vnpu_id) &&
           __atomic_load_n(&state->high_launch_count, __ATOMIC_RELAXED) < limit) {
        mcs_ns_sleep(MCS_WAIT_SLEEP_NS);
    }

    /* Passed the gate: this normal-priority launch consumes the entitlement
     * and restarts the count, so at most one normal launch slips through
     * per burst limit of high-priority launches. */
    (void)__atomic_exchange_n(&state->high_launch_count, 0ULL, __ATOMIC_RELAXED);
    state->last_kernel_time_ns[vnpu_id] = mcs_ns_now();
}
