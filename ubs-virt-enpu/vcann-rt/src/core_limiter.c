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
#include "common.h"
#include "dcmi_wrapper.h"
#include "hash_map.h"
#include "npu_manager.h"
#include "runtime_hook.h"
#include "utils.h"
#include "vnpu_stats.h"

vnpu_time_slice_sched_t *g_vnpu_sched_context = NULL;
uint8_t g_vnpu_id = 0;
volatile int g_terminate = 0;
atomic_bool g_sched_locking = false;
atomic_int hasModelExecuteSync = 0;
pthread_mutex_t g_sched_mutex = PTHREAD_MUTEX_INITIALIZER;
atomic_bool g_monitor_init = false;
static atomic_bool g_aclrt_impl_hook_enable = true;
static atomic_bool g_vnpu_stats_enable = true;

cache_streams_t g_cache_streams = {.num_streams = 0, .streams = {NULL}};

static bool is_normal_priority_vnpu(int vnpu_id);
bool is_vnpu_alive(int vnpu_id);

HashMap *stream_map = NULL;
HashMap *event_map = NULL;

static HashMap *capture_stats_map = NULL;
static HashMap *model_stats_map = NULL;

#define MAX_GRPS_PER_CAPTURE 64

typedef struct {
    uint64_t block_dim;
    uint64_t count;
    rtTaskGrp_t grp_handles[MAX_GRPS_PER_CAPTURE];
    int grp_count;
} stats_buffer_t;

typedef struct {
    int mode;
    rtTaskGrp_t update_handle;
    uint64_t tmp_block_dim;
    uint64_t tmp_count;
} task_grp_state_t;

static HashMap *task_grp_state_map = NULL;
static HashMap *taskGroup_map = NULL;
static pthread_mutex_t g_stats_map_mutex;

uint64_t ns_now(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint64_t)ts.tv_sec * NS_PER_S + (uint64_t)ts.tv_nsec;
}

/// The input must be less than 1000000000.
void ns_sleep(uint64_t ns)
{
    struct timespec req;
    struct timespec rem;
    req.tv_sec = 0;
    req.tv_nsec = ns;
    while (nanosleep(&req, &rem) == -1) {
        if (errno == EINTR) {
            req = rem;
        } else {
            break;
        }
    }
}

bool get_aclrt_impl_hook_enable(void)
{
    return atomic_load(&g_aclrt_impl_hook_enable);
}

void set_aclrt_impl_hook_enable(bool enable)
{
    atomic_store(&g_aclrt_impl_hook_enable, enable);
}

bool get_vnpu_stats_enable(void)
{
    return atomic_load(&g_vnpu_stats_enable);
}

void set_vnpu_stats_enable(bool enable)
{
    atomic_store(&g_vnpu_stats_enable, enable);
}

void restore_streams(rtStream_t stream)
{
    if (stream == NULL) {
        return;
    }

    if (hashmap_contains(stream_map, (void *)stream)) {
        return;
    }

    if (g_cache_streams.num_streams >= MAX_STREAMS_PER_PROCESS) {
        LOG_ERROR("Failed to add stream %p to the cache. Maximum capacity (%d) reached.", (void *)stream,
                  MAX_STREAMS_PER_PROCESS);
        return;
    }

    g_cache_streams.streams[g_cache_streams.num_streams++] = stream;
    int ret = hashmap_put(stream_map, (void *)stream, NULL, false);
    CHECK_COND_RETURN(ret == -1, "Failed to put stream %p to the hash map.", (void *)stream);
    LOG_DEBUG("Stream %p is added in stream hash map.", (void *)stream);
    return;
}

/*
 * Give normal-priority vNPUs a small periodic submission window while any
 * high-priority vNPU is alive. High-priority launches are held during the
 * same window. This is an admission-control guarantee: work already queued on
 * the device cannot be preempted by the host runtime.
 */
static void wait_for_priority_fairness_window(void)
{
    if (g_vnpu_sched_context == NULL) {
        return;
    }

    bool high_priority_alive = false;
    bool normal_priority_alive = false;
    for (int i = 0; i < MAX_VNPU; ++i) {
        if (!is_vnpu_alive(i)) {
            continue;
        }
        if (is_normal_priority_vnpu(i)) {
            normal_priority_alive = true;
        } else {
            high_priority_alive = true;
        }
    }
    if (!high_priority_alive || !normal_priority_alive) {
        return;
    }

    bool is_high_priority = get_vnpu_priority() == VNPU_PRIORITY_HIGH;
    while (!g_terminate) {
        uint64_t phase = ns_now() % VNPU_NORMAL_FAIRNESS_PERIOD;
        bool in_normal_window = phase < VNPU_NORMAL_FAIRNESS_WINDOW;
        if ((is_high_priority && !in_normal_window) || (!is_high_priority && in_normal_window)) {
            return;
        }
        ns_sleep(WAITING_SLEEP_PERIOD);
    }
}

void add_stream(rtStream_t stream)
{
    if (hashmap_contains(stream_map, (void *)stream)) {
        return;
    }
    int ret = hashmap_put(stream_map, (void *)stream, NULL, false);
    CHECK_COND_RETURN(ret == -1, "Failed to put stream %p to the hash map.", (void *)stream);
    LOG_DEBUG("Stream %p is added in stream hash map.", (void *)stream);
    return;
}

void core_limiter(rtStream_t stream, core_function func, void *param)
{
    /* Priority admission must also cover high-priority best-effort vNPUs,
     * which intentionally have no core quota and therefore bypass the normal
     * policy limiter below. */
    wait_for_priority_fairness_window();

    // when schedule policy is 3
    if (!is_core_limit()) {
        return;
    }
    while (!g_terminate) {
        // g_sched_locking is a atomic_int for scheduler thread obtain lock with high priority
        if (atomic_load(&g_sched_locking)) {
            ns_sleep(WAITING_SLEEP_PERIOD);
            continue;
        }
        // waiting for mutex == waiting for launch task
        int rc = pthread_mutex_lock(&g_sched_mutex);
        CHECK_COND_RETURN(rc != 0, "Failed to lock mutex, error code=%d.", rc);
        // double-check g_sched_locking
        if (atomic_load(&g_sched_locking)) {
            pthread_mutex_unlock(&g_sched_mutex);
            ns_sleep(WAITING_SLEEP_PERIOD);
            continue;
        }
        // The delivered stream needs to be recorded because the execution time needs to be collected later.
        restore_streams(stream);
        if (func != NULL) {
            func(param, stream);
        }
        pthread_mutex_unlock(&g_sched_mutex);
        // Recording time when the last task was delivered, which is used for schedule policy 2.
        atomic_store(&g_vnpu_sched_context->last_kernel_time_ns[g_vnpu_id], ns_now());
        return;
    }

    return;
}

bool check_timeout(atomic_uint_fast64_t *timestamp, uint64_t timeout_period)
{
    uint64_t last = atomic_load(timestamp);
    uint64_t now = ns_now();
    // Reboot will recount ns_now() from 0 but not reset timestamp which stored in shared memory.
    // This check is necessary for the problem described above.
    if (last < now) {
        return (now - last <= timeout_period);
    } else {
        return (last - now <= timeout_period);
    }
}

bool is_vnpu_alive(int vnpu_id)
{
    if (vnpu_id < 0 || vnpu_id >= MAX_VNPU) {
        return false;
    }
    return check_timeout(&g_vnpu_sched_context->last_alive_time_ns[vnpu_id], VNPU_TIMEOUT_PERIOD);
}

static bool is_normal_priority_vnpu(int vnpu_id)
{
    return vnpu_id >= 0 && vnpu_id < MAX_VNPU &&
           atomic_load(&g_vnpu_sched_context->vnpu_priority[vnpu_id]) == VNPU_PRIORITY_NORMAL;
}

/* 刷新本 vNPU 的最近任务时间戳: swap 换入完成后由 swap_hook 调用,
 * 供换出决策依据 last kernel time 判断该 vNPU 是否有活跃业务 */
void update_last_kernel_time_now(void)
{
    if (g_vnpu_sched_context == NULL) {
        return;
    }
    atomic_store(&g_vnpu_sched_context->last_kernel_time_ns[g_vnpu_id], ns_now());
}

void vnpu_idling(void)
{
    int npu_core_limit_quota = 0;
    for (int i = 0; i < MAX_VNPU; ++i) {
        if (is_vnpu_alive(i) && is_normal_priority_vnpu(i)) {
            npu_core_limit_quota += atomic_load(&g_vnpu_sched_context->vnpu_core_limit_quota[i]);
        }
        if (npu_core_limit_quota > HUNDRED_PERCENT) {
            return;
        }
    }
    ns_sleep((HUNDRED_PERCENT - npu_core_limit_quota) * VNPU_SCHEULE_PERIOD / HUNDRED_PERCENT);
}

int select_next_owner(int vnpu_id)
{
    int next_vnpu_id = -1;

    for (int i = 1; i <= MAX_VNPU; ++i) {
        int candidate = (vnpu_id + i) % MAX_VNPU;
        if (is_vnpu_alive(candidate) && is_normal_priority_vnpu(candidate)) {
            next_vnpu_id = candidate;
            break;
        }
    }

    return next_vnpu_id;
}

void set_vnpu_and_idle(int vnpu_id, int next_vnpu_id)
{
    if (next_vnpu_id == -1) {
        return;
    }
    if (get_sched_policy() == SCHED_POLICY_FIXED_SHARE && next_vnpu_id <= vnpu_id) {
        vnpu_idling();
    }
    atomic_store(&g_vnpu_sched_context->owner, next_vnpu_id);
}

void synchronize_and_clear_streams(void)
{
    int remaining_count = 0;
    for (int i = 0; i < g_cache_streams.num_streams; ++i) {
        rtStream_t stm = g_cache_streams.streams[i];
        bool capture = 0;
        int rc = hashmap_get_capture_status(stream_map, (void *)stm, &capture);
        CHECK_COND_RETURN(rc == -1, "Failed to get stream %p capture_status from the hash map.", (void *)stm);
        if (capture) {
            LOG_DEBUG("Stream %p is in capture, skip synchronization and clear.", (void *)stm);
            g_cache_streams.streams[remaining_count++] = stm;
            continue;
        }
        LOG_DEBUG("Stream %p is being synchronized.", (void *)stm);
        RUNTIME_HOOK_CALL(rt_library_entry, rtStreamSynchronize, stm);
        rc = hashmap_remove(stream_map, (void *)stm);
        CHECK_COND_RETURN(rc == -1, "Failed to remove stream %p from the hash map.", (void *)stm);
    }
    g_cache_streams.num_streams = remaining_count;
}

// without locks, multiple processes are executed in parallel.
static uint64_t sync_own_streams_and_measure_ns(void)
{
    uint64_t begin = ns_now();
    while (atomic_load(&hasModelExecuteSync) > 0) {
        ns_sleep(WAITING_SLEEP_PERIOD);
    }
    synchronize_and_clear_streams();
    return ns_now() - begin;
}

// wait to finish parallel stream synchronization with all sibling processes of vNPU.
static void wait_for_sibling_sync(uint8_t vnpu_id, uint8_t turn_id)
{
    uint64_t wait_deadline = ns_now() + SYNC_WAIT_TIMEOUT_NS;
    while (ns_now() < wait_deadline) {
        // re-check turn
        if (atomic_load(&g_vnpu_sched_context->vnpu_schedule_turn[vnpu_id]) != turn_id) {
            LOG_DEBUG("vNPU %d pid %d: wait detected turn advanced (turn_id=%u), exit early.", vnpu_id, (int)getpid(),
                      turn_id);
            return;
        }
        int expected = atomic_load(&g_vnpu_sched_context->vnpu_sync_expected[vnpu_id]);
        int completed = atomic_load(&g_vnpu_sched_context->vnpu_sync_completed[vnpu_id]);
        if (completed >= expected) {
            return;
        }
        ns_sleep(WAITING_SLEEP_PERIOD);
    }
    int expected = atomic_load(&g_vnpu_sched_context->vnpu_sync_expected[vnpu_id]);
    int completed = atomic_load(&g_vnpu_sched_context->vnpu_sync_completed[vnpu_id]);
    LOG_WARN("vNPU %d pid %d: sync wait timeout, completed=%d expected=%d. "
             "Slow sibling's sync not counted in debt this turn.",
             vnpu_id, (int)getpid(), completed, expected);
}

// multiple processes fetch and consume time slices from shared memory
uint64_t add_and_consume_time_slice(uint8_t *turn_id, int *next_vnpu_id)
{
    // processes of the same vNPU cannot inject quota/deport timeslice concurrently
    int rc = pthread_mutex_lock(&g_vnpu_sched_context->vnpu_schedule_mutex[g_vnpu_id]);
    if (rc == EOWNERDEAD) {
        LOG_INFO("Consumer mutex owner died; taking over for vNPU %d.", g_vnpu_id);
        pthread_mutex_consistent(&g_vnpu_sched_context->vnpu_schedule_mutex[g_vnpu_id]);
    } else if (rc != 0) {
        LOG_WARN("Failed to lock vnpu_schedule_mutex, rc=%d.", rc);
        *turn_id = atomic_load(&g_vnpu_sched_context->vnpu_schedule_turn[g_vnpu_id]);
        *next_vnpu_id = -1;
        return 0;
    }

    uint8_t cur_turn = atomic_load(&g_vnpu_sched_context->vnpu_schedule_turn[g_vnpu_id]);

    // only one process can operate this round of quota
    if (cur_turn != atomic_load(&g_vnpu_sched_context->vnpu_quota_injected_for_turn[g_vnpu_id])) {
        atomic_store(&g_vnpu_sched_context->vnpu_sync_expected[g_vnpu_id], 0);
        atomic_store(&g_vnpu_sched_context->vnpu_sync_completed[g_vnpu_id], 0);
        atomic_store(&g_vnpu_sched_context->vnpu_turn_max_sync[g_vnpu_id], 0ULL);

        uint64_t quota = atomic_load(&g_vnpu_sched_context->vnpu_quota_timeslice[g_vnpu_id]);
        atomic_fetch_add(&g_vnpu_sched_context->vnpu_cur_timeslice[g_vnpu_id], quota);

        int64_t cur_after_inject = (int64_t)atomic_load(&g_vnpu_sched_context->vnpu_cur_timeslice[g_vnpu_id]);
        uint64_t ts = cur_after_inject > 0 ? (uint64_t)cur_after_inject : 0ULL;
        if (ts > 0) {
            atomic_fetch_sub(&g_vnpu_sched_context->vnpu_cur_timeslice[g_vnpu_id], ts);
        }
        // for multi processes: record the current round's time slice quota into the shared memory
        atomic_store(&g_vnpu_sched_context->vnpu_timeslice_for_turn[g_vnpu_id], ts);
        atomic_store(&g_vnpu_sched_context->vnpu_quota_injected_for_turn[g_vnpu_id], cur_turn);

        LOG_DEBUG("Quota %llu ns injected for vNPU %d turn %u; timeslice=%llu ns.", (unsigned long long)quota,
                  g_vnpu_id, cur_turn, (unsigned long long)ts);
    }

    atomic_fetch_add(&g_vnpu_sched_context->vnpu_sync_expected[g_vnpu_id], 1);

    *turn_id = cur_turn;
    int owner = atomic_load(&g_vnpu_sched_context->owner);
    *next_vnpu_id = select_next_owner(owner);

    // multiple processes consume time slices in parallel
    pthread_mutex_unlock(&g_vnpu_sched_context->vnpu_schedule_mutex[g_vnpu_id]);
    uint64_t ts = atomic_load(&g_vnpu_sched_context->vnpu_timeslice_for_turn[g_vnpu_id]);
    if (ts > 0) {
        atomic_store(&g_sched_locking, false);
        uint64_t end = ns_now() + ts;
        while (ns_now() < end && !g_terminate) {
            if (atomic_load(&g_vnpu_sched_context->owner) != g_vnpu_id) {
                break;
            }
            ns_sleep(WAITING_SLEEP_PERIOD);
        }
        atomic_store(&g_sched_locking, true);
    }

    return ts;
}

void *vnpu_scheduler_flush_thread(void *arg)
{
    (void)arg;
    while (!g_terminate) {
        uint64_t now = ns_now();
        atomic_store(&g_vnpu_sched_context->last_alive_time_ns[g_vnpu_id], now);
        ns_sleep(VNPU_FLUSH_PERIOD);
    }
    return NULL;
}

int calculate_alive_vnpu_num(void)
{
    int count = 0;
    for (size_t i = 0; i < MAX_VNPU; i++) {
        if (is_vnpu_alive(i)) {
            count++;
        }
    }
    return count;
}

void *npu_utilization_monitor_thread(void *arg)
{
    (void)arg;
    int rc = pthread_mutex_lock(&g_vnpu_sched_context->npu_utilization_monitor_mutex);
    if (rc == EOWNERDEAD) {
        pthread_mutex_consistent(&g_vnpu_sched_context->npu_utilization_monitor_mutex);
    } else if (rc != 0) {
        LOG_WARN("Failed to obtain mutex lock, error code=%d.", rc);
        return NULL;
    }
    int owner = atomic_load(&g_vnpu_sched_context->owner);
    while (owner == g_vnpu_id) {
        if (check_timeout(&g_vnpu_sched_context->last_slide_window_time_ns, WATTING_SLIDE_WINDOW_TIMEOUT_PERIOD)) {
            ns_sleep(VNPU_FLUSH_PERIOD);
            owner = atomic_load(&g_vnpu_sched_context->owner);
            continue;
        }
        unsigned int utilization_rate = 0;
        uint64_t begin = ns_now();
        atomic_store(&g_vnpu_sched_context->last_slide_window_time_ns, begin);
        int ret =
            enpu_dcmi_get_device_utilization_rate(get_logic_id(), get_card_id(), get_device_id(), &utilization_rate);
        if (ret != ENPU_SUCCESS) {
            LOG_ERROR("DCMI call failed with ret: %d.", ret);
            owner = atomic_load(&g_vnpu_sched_context->owner);
            continue;
        }

        uint64_t now = ns_now();
        uint64_t diff_ns = now - begin;
        if (diff_ns > DCMI_TIMEOUT_THRESHOLD) {
            LOG_DEBUG("The DCMI interface is overloaded, reuse the NPU utilization status from the last time.");
            owner = atomic_load(&g_vnpu_sched_context->owner);
            continue;
        }

        static int high_load_streak = 0;
        static int low_load_streak = 0;
        int current_window = atomic_load(&g_vnpu_sched_context->slide_window_len);
        int new_window = current_window;

        if (utilization_rate > UTILIZATION_RATE_MAX) {
            low_load_streak = 0;
            high_load_streak++;
            if (high_load_streak >= MAX_STREAK && current_window > 0) {
                new_window = current_window - 1;
                high_load_streak = 0;
                LOG_DEBUG("Utilization high (%u%%), decreasing window to %d.", utilization_rate, new_window);
            }
        } else if (utilization_rate < UTILIZATION_RATE_MIN) {
            high_load_streak = 0;
            low_load_streak++;
            if (low_load_streak >= MAX_STREAK) {
                int max_len = calculate_alive_vnpu_num() - 1;
                max_len = (max_len < 0) ? 0 : max_len;
                if (current_window < max_len) {
                    new_window = current_window + 1;
                    LOG_DEBUG("Utilization low (%u%%), increasing window to %d (max:%d).", utilization_rate, new_window,
                              max_len);
                }
                low_load_streak = 0;
            }
        } else {
            high_load_streak = 0;
            low_load_streak = 0;
        }

        if (new_window != current_window) {
            atomic_store(&g_vnpu_sched_context->slide_window_len, new_window);
        }
        ns_sleep(VNPU_FLUSH_PERIOD);
        owner = atomic_load(&g_vnpu_sched_context->owner);
    }
    atomic_store(&g_monitor_init, false);
    pthread_mutex_unlock(&g_vnpu_sched_context->npu_utilization_monitor_mutex);
    return NULL;
}

bool slide_window_check(int owner)
{
    int slide_windows_len = atomic_load(&g_vnpu_sched_context->slide_window_len);

    for (int i = 1; i <= MAX_VNPU && slide_windows_len > 0; ++i) {
        int next_vnpu = (owner + i) % MAX_VNPU;
        if (next_vnpu == g_vnpu_id) {
            return true;
        }
        // The slide window only contains alive vnpu.
        if (is_vnpu_alive(next_vnpu) && is_normal_priority_vnpu(next_vnpu)) {
            slide_windows_len -= 1;
        }
    }
    return false;
}

void check_and_borrow_timeslice(int owner)
{
    if (owner == g_vnpu_id) {
        // Check and update slide_window_len, no borrow here
        if (!atomic_load(&g_monitor_init)) {
            atomic_store(&g_monitor_init, true);
            pthread_t thread;
            int rc = pthread_create(&thread, NULL, npu_utilization_monitor_thread, NULL);
            CHECK_ERROR_CODE(rc, "Failed to create npu_utilization_monitor_thread.");
            pthread_detach(thread);
        }
    } else if (slide_window_check(owner)) { // Check and borrow timeslice
        atomic_store(&g_sched_locking, false);
        ns_sleep(BORROW_TIMESLICE_LENGTH); // borrow BORROW_TIMESLICE_LENGTH ns every time
        atomic_store(&g_sched_locking, true);
    }
}

// Scheduling main thread
void *vnpu_scheduler_thread(void *arg)
{
    (void)arg;
    uint8_t turn_id = -1;
    int next_vnpu_id = -1;
    uint64_t last_avg_update_ns = 0; /* avg_duration 每 VNPU_STATS_WINDOW_NS 刷一次 */
    // g_sched_locking = true : user can not launch task by core_limiter
    atomic_store(&g_sched_locking, true);
    while (!g_terminate) {
        // Distributed thread scheduling.
        // Scheduling is performed only when the owner is the current vnpu or the owner is disabled.
        int owner = atomic_load(&g_vnpu_sched_context->owner);

        // ELASTIC will consider borrow timeslice
        if (get_sched_policy() == SCHED_POLICY_ELASTIC) {
            check_and_borrow_timeslice(owner);
        }

        if (owner != g_vnpu_id) {
            if (!is_vnpu_alive(owner)) {
                int vnpu_id = atomic_load(&g_vnpu_sched_context->owner);
                set_vnpu_and_idle(vnpu_id, select_next_owner(vnpu_id));
            }
            ns_sleep(WAITING_SLEEP_PERIOD);
            continue;
        }

        uint64_t now = ns_now();
        if (now - last_avg_update_ns >= VNPU_STATS_WINDOW_NS) {
            last_avg_update_ns = now;
            uint64_t kernel_count = 0;
            uint64_t kernel_avg_time = get_ker_ave_exec_time(&kernel_count);
            vnpu_stats_set_local_avg_duration(get_vnpu_id(), kernel_avg_time, kernel_count);
        }

        // Consumption time slice. The lock is released to the user process within the specified time.
        uint64_t timeslice = add_and_consume_time_slice(&turn_id, &next_vnpu_id);

        if (timeslice > 0) {
            // without locks, multiple processes are executed in parallel.
            uint64_t my_sync_ns = sync_own_streams_and_measure_ns();
            if (atomic_load(&g_vnpu_sched_context->vnpu_schedule_turn[g_vnpu_id]) == turn_id) {
                atomic_fetch_max_uint64(&g_vnpu_sched_context->vnpu_turn_max_sync[g_vnpu_id], my_sync_ns);
                atomic_fetch_add(&g_vnpu_sched_context->vnpu_sync_completed[g_vnpu_id], 1);
                LOG_DEBUG("vNPU %d pid %d: parallel sync done, my_elapsed=%llu ns.", g_vnpu_id, (int)getpid(),
                          (unsigned long long)my_sync_ns);
            } else {
                // this process flow synchronization timeout, data discarded
                LOG_WARN("vNPU %d pid %d: sync took %llu ns, turn changed during sync (stale, skip).", g_vnpu_id,
                         (int)getpid(), (unsigned long long)my_sync_ns);
                continue;
            }
        } else {
            atomic_fetch_add(&g_vnpu_sched_context->vnpu_sync_completed[g_vnpu_id], 1);
        }

        // wait to finish parallel stream synchronization with all sibling processes of vNPU.
        wait_for_sibling_sync(g_vnpu_id, turn_id);

        // Only one thread is accepted.
        int rc = pthread_mutex_lock(&g_vnpu_sched_context->vnpu_schedule_mutex[g_vnpu_id]);
        if (rc == EOWNERDEAD) {
            LOG_INFO("The scheduling process has been detected to exit, and the scheduling is being taken over.");
            pthread_mutex_consistent(&g_vnpu_sched_context->vnpu_schedule_mutex[g_vnpu_id]);
        } else if (rc != 0) {
            LOG_WARN("Failed to obtain mutex lock, error code=%d.", rc);
            continue;
        }

        if (atomic_load(&g_vnpu_sched_context->vnpu_schedule_turn[g_vnpu_id]) == turn_id) {
            // Only the slice of the main process is considered.
            if (timeslice > 0) {
                uint64_t debt = atomic_load(&g_vnpu_sched_context->vnpu_turn_max_sync[g_vnpu_id]);
                (void)atomic_fetch_sub(&g_vnpu_sched_context->vnpu_cur_timeslice[g_vnpu_id], debt);
                LOG_DEBUG("Leader vNPU %d turn %u: applied debt=%llu ns (max of sibling syncs).", g_vnpu_id, turn_id,
                          (unsigned long long)debt);
            }

            set_vnpu_and_idle(atomic_load(&g_vnpu_sched_context->owner), next_vnpu_id);
            atomic_store(&g_vnpu_sched_context->vnpu_schedule_turn[g_vnpu_id], turn_id + 1);
        }
        pthread_mutex_unlock(&g_vnpu_sched_context->vnpu_schedule_mutex[g_vnpu_id]);
    }
    hashmap_destroy(stream_map);
    hashmap_destroy(event_map);
    return NULL;
}

#define SCHED_SHM_STALE_THRESHOLD_NS (2ULL * NS_PER_S)
/* 判断调度 shm 是否属于残留内存 */
static bool sched_shm_is_stale(void)
{
    uint64_t now = ns_now();
    uint64_t newest_alive = 0;
    for (int i = 0; i < MAX_VNPU; ++i) {
        uint64_t t = atomic_load(&g_vnpu_sched_context->last_alive_time_ns[i]);
        if (t > newest_alive) {
            newest_alive = t;
        }
    }
    if (newest_alive == 0) {
        return false; /* 从未有心跳、全新初始化场景, 交给 magic 处理 */
    }
    return (now > newest_alive) && ((now - newest_alive) > SCHED_SHM_STALE_THRESHOLD_NS);
}

int share_mem_init(vnpu_time_slice_sched_t *vnpu_sched_shm)
{
    g_vnpu_sched_context = vnpu_sched_shm;
    uint64_t begin = ns_now();

    while (!g_terminate) {
        if (atomic_load(&g_vnpu_sched_context->magic_number) == MAGIC_INITIALIZED) {
            if (!sched_shm_is_stale()) {
                return ENPU_SUCCESS; /* 同轮次有进程心跳, 正常复用 */
            }
            LOG_INFO("Sched shm belongs to a dead run (all alive-heartbeats stale), re-initializing.");
            atomic_store(&g_vnpu_sched_context->magic_number, MAGIC_UNINITIALIZED);
            continue;
        }

        if (atomic_load(&g_vnpu_sched_context->magic_number) == MAGIC_INITIALIZING) {
            uint64_t now = ns_now();
            if (now - begin > VNPU_TIMEOUT_PERIOD) {
                atomic_store(&g_vnpu_sched_context->magic_number, MAGIC_UNINITIALIZED);
                begin = now;
            }
            ns_sleep(WAITING_SLEEP_PERIOD);
            continue;
        }

        atomic_store(&g_vnpu_sched_context->magic_number, MAGIC_INITIALIZING);
        atomic_store(&g_vnpu_sched_context->owner, -1);

        pthread_mutexattr_t attr;
        pthread_mutexattr_init(&attr);
        pthread_mutexattr_setpshared(&attr, PTHREAD_PROCESS_SHARED);
        pthread_mutexattr_setrobust(&attr, PTHREAD_MUTEX_ROBUST);

        for (int i = 0; i < MAX_VNPU; ++i) {
            atomic_store(&g_vnpu_sched_context->last_alive_time_ns[i], 0ULL);
            atomic_store(&g_vnpu_sched_context->last_kernel_time_ns[i], 0ULL);
            atomic_store(&g_vnpu_sched_context->vnpu_core_limit_quota[i], 0);
            atomic_store(&g_vnpu_sched_context->vnpu_priority[i], VNPU_PRIORITY_NORMAL);
            atomic_store(&g_vnpu_sched_context->vnpu_schedule_turn[i], 0);
            atomic_store(&g_vnpu_sched_context->vnpu_quota_timeslice[i], 0ULL);
            atomic_store(&g_vnpu_sched_context->vnpu_cur_timeslice[i], 0ULL);
            atomic_store(&g_vnpu_sched_context->vnpu_quota_injected_for_turn[i], (uint_fast8_t)0xFF);
            atomic_store(&g_vnpu_sched_context->vnpu_sync_expected[i], 0);
            atomic_store(&g_vnpu_sched_context->vnpu_sync_completed[i], 0);
            atomic_store(&g_vnpu_sched_context->vnpu_turn_max_sync[i], 0ULL);
            atomic_store(&g_vnpu_sched_context->vnpu_timeslice_for_turn[i], 0ULL);
            int ret = pthread_mutex_init(&g_vnpu_sched_context->vnpu_schedule_mutex[i], &attr);
            if (ret != 0) {
                LOG_ERROR("Failed to init vnpu_schedule_mutex[%d], error=%d.", i, ret);
                pthread_mutexattr_destroy(&attr);
                return ENPU_FAIL;
            }
        }

        int ret = pthread_mutex_init(&g_vnpu_sched_context->npu_utilization_monitor_mutex, &attr);
        if (ret != 0) {
            LOG_ERROR("Failed to init npu_utilization_monitor_mutex, error=%d.", ret);
            pthread_mutexattr_destroy(&attr);
            return ENPU_FAIL;
        }
        pthread_mutexattr_destroy(&attr);
        atomic_store(&g_vnpu_sched_context->magic_number, MAGIC_INITIALIZED);
        return ENPU_SUCCESS;
    }
    return ENPU_FAIL;
}

int vnpu_scheduler_init(vnpu_time_slice_sched_t *vnpu_sched_shm)
{
    g_vnpu_sched_context = vnpu_sched_shm;
    g_vnpu_id = get_vnpu_id();

    uint8_t aicore_limit_percent = get_core_limit_quota();
    uint8_t vnpu_priority = get_vnpu_priority();
    atomic_store(&g_vnpu_sched_context->vnpu_priority[g_vnpu_id], vnpu_priority);
    if (vnpu_priority == VNPU_PRIORITY_HIGH) {
        // High priority vNPU is excluded from owner rotation and runs tasks directly.
        return ENPU_SUCCESS;
    }
    atomic_store(&g_vnpu_sched_context->vnpu_core_limit_quota[g_vnpu_id], aicore_limit_percent);
    uint64_t aicore_cur_timesilice = aicore_limit_percent * VNPU_SCHEULE_PERIOD / HUNDRED_PERCENT;
    atomic_store(&g_vnpu_sched_context->vnpu_quota_timeslice[g_vnpu_id], aicore_cur_timesilice);

    return ENPU_SUCCESS;
}

int vnpu_scheduler_start(void)
{
    int rc = 0;
    if (get_vnpu_priority() == VNPU_PRIORITY_NORMAL) {
        pthread_t vnpu_scheduler_tid;
        rc = pthread_create(&vnpu_scheduler_tid, NULL, vnpu_scheduler_thread, NULL);
        if (rc != 0) {
            LOG_ERROR("Failed to create vnpu scheduler thread, error=%d.", rc);
            return ENPU_FAIL;
        }
        pthread_detach(vnpu_scheduler_tid);
    }
    /* 立即 detach：下面那次 create 一旦失败就直接返回, 再也无法 join 这个线程, 
     * 不 detach 会泄漏线程描述符和栈.  */
    pthread_t vnpu_alive_tid;
    rc = pthread_create(&vnpu_alive_tid, NULL, vnpu_scheduler_flush_thread, NULL);
    if (rc != 0) {
        LOG_ERROR("Failed to create vnpu alive thread, error=%d.", rc);
        return ENPU_FAIL;
    }
    pthread_detach(vnpu_alive_tid);
    return ENPU_SUCCESS;
}

/* 统一回收 aicore_limiter_initialize 里创建的 hashmap. 
 * 这些指针都是文件级变量、初值 NULL, 且 aicore_limiter_initialize 由 pthread_once
 * 保证只执行一次, 所以"尚未创建"等价于"仍为 NULL", 可在任意失败点无条件调用.  */
static void destroy_all_stats_maps(void)
{
    HashMap **maps[] = {&taskGroup_map,     &task_grp_state_map, &model_stats_map,
                        &capture_stats_map, &event_map,          &stream_map};
    for (size_t i = 0; i < sizeof(maps) / sizeof(maps[0]); i++) {
        if (*maps[i] != NULL) {
            hashmap_destroy(*maps[i]);
            *maps[i] = NULL;
        }
    }
}

int aicore_limiter_initialize(void)
{
    int rc = ENPU_FAIL;
    vnpu_time_slice_sched_t *vnpu_sched_shm = NULL;
    vnpu_sched_shm = map_share_mem(get_vnpu_shm_id(), sizeof(vnpu_time_slice_sched_t));
    if (vnpu_sched_shm == NULL) {
        LOG_ERROR("Failed to mmap share memory.");
        return ENPU_FAIL;
    }

    rc = share_mem_init(vnpu_sched_shm);
    if (rc != ENPU_SUCCESS) {
        LOG_ERROR("Failed to initialize shared memory.");
        goto err_unmap;
    }

    rc = vnpu_scheduler_init(vnpu_sched_shm);
    if (rc != ENPU_SUCCESS) {
        LOG_ERROR("Failed to initialize vnpu scheduler.");
        goto err_unmap;
    }

    stream_map = hashmap_create(MAX_STREAMS_PER_PROCESS);
    if (!stream_map) {
        LOG_ERROR("Stream hash map init failed.");
        goto err_unmap;
    }

    event_map = hashmap_create(MAX_EVENT_PER_PROCESS);
    if (!event_map) {
        LOG_ERROR("Event hash map init failed.");
        goto err_destroy_maps;
    }

    capture_stats_map = hashmap_create(MAX_STREAMS_PER_PROCESS);
    if (!capture_stats_map) {
        LOG_ERROR("Capture stats hash map init failed.");
        goto err_destroy_maps;
    }

    model_stats_map = hashmap_create(MAX_STREAMS_PER_PROCESS);
    if (!model_stats_map) {
        LOG_ERROR("Model stats hash map init failed.");
        goto err_destroy_maps;
    }

    task_grp_state_map = hashmap_create(MAX_STREAMS_PER_PROCESS);
    if (!task_grp_state_map) {
        LOG_ERROR("Task grp state hash map init failed.");
        goto err_destroy_maps;
    }

    taskGroup_map = hashmap_create(MAX_EVENT_PER_PROCESS);
    if (!taskGroup_map) {
        LOG_ERROR("TaskGroup hash map init failed.");
        goto err_destroy_maps;
    }

    pthread_mutexattr_t stats_attr;
    pthread_mutexattr_init(&stats_attr);
    pthread_mutexattr_settype(&stats_attr, PTHREAD_MUTEX_RECURSIVE);
    int ret = pthread_mutex_init(&g_stats_map_mutex, &stats_attr);
    pthread_mutexattr_destroy(&stats_attr);
    if (ret != 0) {
        LOG_ERROR("Failed to init g_stats_map_mutex, error=%d.", ret);
        /* init 失败的 mutex 不能 destroy, 直接去回收 hashmap */
        goto err_destroy_maps;
    }

    rc = vnpu_scheduler_start();
    if (rc != ENPU_SUCCESS) {
        LOG_ERROR("Failed to start vnpu scheduler threads.");
        /* 此时调度线程可能已经跑起来, 因此一律不回收, 全部留给进程退出. */
        return ENPU_FAIL;
    }

    return ENPU_SUCCESS;

err_destroy_maps:
    destroy_all_stats_maps();
err_unmap:
    unmap_share_mem(vnpu_sched_shm, sizeof(*vnpu_sched_shm));
    /* share_mem_init / vnpu_scheduler_init 已经把 g_vnpu_sched_context 指向了这块
     * 共享内存, unmap 之后必须置 NULL, 否则 core_limiter()、is_vnpu_alive() 等
     * 无 NULL 检查的解引用点会访问已解除映射的内存.  */
    g_vnpu_sched_context = NULL;
    return ENPU_FAIL;
}

void set_stream_capture(void *param, rtStream_t stream)
{
    bool capture = *(bool *)param;
    if (!capture) {
        for (int i = 0; i < g_cache_streams.num_streams; ++i) {
            rtStream_t stm = g_cache_streams.streams[i];
            void *head_stream = NULL;
            int rc = hashmap_get_ptr(stream_map, (void *)stm, &head_stream);
            CHECK_COND_RETURN(rc == -1, "Failed to get stream %p ptr from the hash map.", (void *)stm);
            if (head_stream == (void *)stream) {
                LOG_DEBUG("Stream %p capture state set to: 0.", (void *)stream);
                rc = hashmap_put(stream_map, (void *)stm, NULL, false);
                CHECK_COND_RETURN(rc == -1, "Failed to put stream %p to the hash map.", (void *)stm);
            }
        }
    } else {
        int rc = hashmap_put(stream_map, (void *)stream, (void *)stream, capture);
        CHECK_COND_RETURN(rc == -1, "Failed to put stream %p to the hash map.", (void *)stream);
    }
    LOG_DEBUG("Stream %p capture state set to: %d.", (void *)stream, capture ? 1 : 0);
}

void set_event_wait_status(void *evt, rtStream_t stm)
{
    MapValue event_status;
    int rc = hashmap_get(event_map, evt, &event_status);
    CHECK_COND_RETURN(rc == -1, "Error: Event hash map get event %p failed.", evt);

    // not capture stream
    if (event_status.ptr != NULL) {
        // update capture status by event
        rc = hashmap_put(stream_map, (void *)stm, event_status.ptr, true);
        CHECK_COND_RETURN(rc == -1, "Failed to put stream %p to the hash map.", (void *)stm);
        LOG_DEBUG("Stream %p capture state set to: true, because of event.", (void *)stm);
    }
}

void set_event_create_status(void *evt)
{
    int rc = hashmap_put(event_map, evt, NULL, false);
    CHECK_COND_RETURN(rc == -1, "Error: Event hash map put event %p failed.", evt);
}

void set_event_record_status(void *evt, rtStream_t stm)
{
    MapValue event_status;
    int rc = hashmap_get(event_map, evt, &event_status);
    CHECK_COND_RETURN(rc == -1, "Error: Event hash map get event %p failed.", evt);
    void *head_stream = NULL;
    rc = hashmap_get_ptr(stream_map, (void *)stm, &head_stream);
    CHECK_COND_RETURN(rc == -1, "Failed to get stream %p ptr from the hash map.", (void *)stm);
    // capture
    if (head_stream != NULL) {
        rc = hashmap_put(event_map, evt, head_stream, true);
        CHECK_COND_RETURN(rc == -1, "Error: Event hash map put event %p failed.", evt);
        LOG_DEBUG("Event %p capture status is updated to true in recording.", evt);
    }
}

void remove_stream(void *unused, rtStream_t stm)
{
    (void)unused;
    LOG_DEBUG("Remove stream %p", stm);
    for (int i = 0; i < g_cache_streams.num_streams; ++i) {
        if (stm == g_cache_streams.streams[i]) {
            for (int j = i + 1; j < g_cache_streams.num_streams; ++j) {
                g_cache_streams.streams[j - 1] = g_cache_streams.streams[j];
            }
            g_cache_streams.num_streams -= 1;
            (void)hashmap_remove(stream_map, (void *)stm);
            LOG_DEBUG("Stream position %d removed.", i);
            break;
        }
    }
}

void set_event_destroy_status(void *evt)
{
    (void)hashmap_remove(event_map, evt);
}

bool stream_is_capturing(rtStream_t stm)
{
    if (stream_map == NULL || stm == NULL) {
        return false;
    }
    bool capturing = false;
    int rc = hashmap_get_capture_status(stream_map, (void *)stm, &capturing);
    if (rc != 0) {
        return false;
    }
    return capturing;
}

void capture_stats_add(rtStream_t stm, uint32_t block_dim)
{
    if (capture_stats_map == NULL || stm == NULL) {
        return;
    }
    pthread_mutex_lock(&g_stats_map_mutex);
    void *ptr = NULL;
    stats_buffer_t *buf = NULL;
    if (hashmap_get_ptr(capture_stats_map, (void *)stm, &ptr) == 0 && ptr != NULL) {
        buf = (stats_buffer_t *)ptr;
    } else {
        buf = (stats_buffer_t *)calloc(1, sizeof(stats_buffer_t));
        if (buf == NULL) {
            LOG_ERROR("Failed to alloc capture stats buffer for stream %p.", (void *)stm);
            pthread_mutex_unlock(&g_stats_map_mutex);
            return;
        }
        if (hashmap_put(capture_stats_map, (void *)stm, (void *)buf, false) != 0) {
            LOG_WARN("Failed to insert capture stats entry for stream %p, stats dropped.", (void *)stm);
            free(buf);
            pthread_mutex_unlock(&g_stats_map_mutex);
            return;
        }
    }
    buf->block_dim += block_dim;
    buf->count += 1;
    pthread_mutex_unlock(&g_stats_map_mutex);
}

int capture_stats_transfer_to_model(rtStream_t stm, rtModel_t mdl)
{
    if (capture_stats_map == NULL || model_stats_map == NULL || stm == NULL || mdl == NULL) {
        return -1;
    }
    pthread_mutex_lock(&g_stats_map_mutex);
    void *stream_ptr = NULL;
    if (hashmap_get_ptr(capture_stats_map, (void *)stm, &stream_ptr) != 0 || stream_ptr == NULL) {
        LOG_DEBUG("No capture stats for stream %p during transfer.", (void *)stm);
        pthread_mutex_unlock(&g_stats_map_mutex);
        return -1;
    }
    stats_buffer_t *stream_buf = (stats_buffer_t *)stream_ptr;

    /* 累加到 model */
    void *model_ptr = NULL;
    stats_buffer_t *model_buf = NULL;
    if (hashmap_get_ptr(model_stats_map, (void *)mdl, &model_ptr) == 0 && model_ptr != NULL) {
        model_buf = (stats_buffer_t *)model_ptr;
    } else {
        model_buf = (stats_buffer_t *)calloc(1, sizeof(stats_buffer_t));
        if (model_buf == NULL) {
            LOG_ERROR("Failed to alloc model stats buffer for model %p.", (void *)mdl);
            pthread_mutex_unlock(&g_stats_map_mutex);
            return -1;
        }
        if (hashmap_put(model_stats_map, (void *)mdl, (void *)model_buf, false) != 0) {
            LOG_WARN("Failed to insert model stats entry for model %p, stats dropped.", (void *)mdl);
            free(model_buf);
            pthread_mutex_unlock(&g_stats_map_mutex);
            return -1;
        }
    }
    model_buf->block_dim += stream_buf->block_dim;
    model_buf->count += stream_buf->count;
    /* group handle 以引用方式追加到 model */
    for (int i = 0; i < stream_buf->grp_count; ++i) {
        if (model_buf->grp_count >= MAX_GRPS_PER_CAPTURE) {
            LOG_WARN("Model %p grp handles overflow (max %d), rest groups not tracked.", (void *)mdl,
                     MAX_GRPS_PER_CAPTURE);
            break;
        }
        model_buf->grp_handles[model_buf->grp_count++] = stream_buf->grp_handles[i];
    }

    /* 清空 stream */
    stream_buf->block_dim = 0;
    stream_buf->count = 0;
    stream_buf->grp_count = 0;
    pthread_mutex_unlock(&g_stats_map_mutex);
    return 0;
}

static uint64_t task_group_map_get(rtTaskGrp_t handle);

static uint64_t model_stats_total_block_dim(stats_buffer_t *buf)
{
    uint64_t total = buf->block_dim;
    for (int i = 0; i < buf->grp_count; ++i) {
        total += task_group_map_get(buf->grp_handles[i]);
    }
    return total;
}

int model_stats_get(rtModel_t mdl, uint64_t *block_dim, uint64_t *count)
{
    if (model_stats_map == NULL || mdl == NULL) {
        return -1;
    }
    pthread_mutex_lock(&g_stats_map_mutex);
    void *ptr = NULL;
    if (hashmap_get_ptr(model_stats_map, (void *)mdl, &ptr) != 0 || ptr == NULL) {
        pthread_mutex_unlock(&g_stats_map_mutex);
        return -1;
    }
    stats_buffer_t *buf = (stats_buffer_t *)ptr;
    if (block_dim != NULL) {
        *block_dim = model_stats_total_block_dim(buf);
    }
    if (count != NULL) {
        *count = buf->count;
    }
    pthread_mutex_unlock(&g_stats_map_mutex);
    return 0;
}

/* 查询 stream 的 task-group 状态, 无则返回 NULL（返回值仅在持锁上下文内使用） */
static task_grp_state_t *task_grp_state_get(rtStream_t stm)
{
    if (task_grp_state_map == NULL || stm == NULL) {
        return NULL;
    }
    void *ptr = NULL;
    if (hashmap_get_ptr(task_grp_state_map, (void *)stm, &ptr) != 0 || ptr == NULL) {
        return NULL;
    }
    return (task_grp_state_t *)ptr;
}

static task_grp_state_t *task_grp_state_reset(rtStream_t stm)
{
    if (task_grp_state_map == NULL || stm == NULL) {
        return NULL;
    }
    void *ptr = NULL;
    task_grp_state_t *st = NULL;
    if (hashmap_get_ptr(task_grp_state_map, (void *)stm, &ptr) == 0 && ptr != NULL) {
        st = (task_grp_state_t *)ptr;
    } else {
        st = (task_grp_state_t *)calloc(1, sizeof(task_grp_state_t));
        if (st == NULL) {
            LOG_ERROR("Failed to alloc task grp state for stream %p.", (void *)stm);
            return NULL;
        }
        if (hashmap_put(task_grp_state_map, (void *)stm, (void *)st, false) != 0) {
            LOG_WARN("Failed to insert task grp state for stream %p.", (void *)stm);
            free(st);
            return NULL;
        }
    }
    st->mode = 0;
    st->update_handle = NULL;
    st->tmp_block_dim = 0;
    st->tmp_count = 0;
    return st;
}

/* 写入/覆盖 group 的 blockDim（已存在则覆盖刷新） */
static void task_group_map_set(rtTaskGrp_t handle, uint64_t block_dim)
{
    if (taskGroup_map == NULL || handle == NULL) {
        return;
    }
    void *ptr = NULL;
    uint64_t *val = NULL;
    if (hashmap_get_ptr(taskGroup_map, (void *)handle, &ptr) == 0 && ptr != NULL) {
        val = (uint64_t *)ptr;
    } else {
        val = (uint64_t *)calloc(1, sizeof(uint64_t));
        if (val == NULL) {
            LOG_ERROR("Failed to alloc taskGroup_map entry for handle %p.", (void *)handle);
            return;
        }
        if (hashmap_put(taskGroup_map, (void *)handle, (void *)val, false) != 0) {
            LOG_WARN("Failed to insert taskGroup_map entry for handle %p, stats dropped.", (void *)handle);
            free(val);
            return;
        }
    }
    *val = block_dim;
}

static uint64_t task_group_map_get(rtTaskGrp_t handle)
{
    if (taskGroup_map == NULL || handle == NULL) {
        return 0;
    }
    void *ptr = NULL;
    if (hashmap_get_ptr(taskGroup_map, (void *)handle, &ptr) != 0 || ptr == NULL) {
        return 0;
    }
    return *(uint64_t *)ptr;
}

void task_grp_begin(rtStream_t stm)
{
    pthread_mutex_lock(&g_stats_map_mutex);
    task_grp_state_t *st = task_grp_state_reset(stm);
    if (st == NULL) {
        pthread_mutex_unlock(&g_stats_map_mutex);
        return;
    }
    st->mode = 1; /* IN_GRP */
    pthread_mutex_unlock(&g_stats_map_mutex);
}

void task_grp_end(rtStream_t stm, rtTaskGrp_t handle)
{
    pthread_mutex_lock(&g_stats_map_mutex);
    task_grp_state_t *st = task_grp_state_get(stm);
    if (st == NULL || st->mode != 1) {
        pthread_mutex_unlock(&g_stats_map_mutex);
        return;
    }
    /* group 的 blockDim 到 taskGroup_map（Update 之前构建值生效） */
    task_group_map_set(handle, st->tmp_block_dim);

    /* 把 handle 记到当前 stream 的 capture_stats_map（EndCapture 时随图转移到 model）.
     * 无条目时创建条目, 保证非 capture 场景的 group 也能登记（后续 EndCapture 可转移）. */
    if (capture_stats_map != NULL) {
        void *ptr = NULL;
        stats_buffer_t *cap = NULL;
        if (hashmap_get_ptr(capture_stats_map, (void *)stm, &ptr) == 0 && ptr != NULL) {
            cap = (stats_buffer_t *)ptr;
        } else {
            cap = (stats_buffer_t *)calloc(1, sizeof(stats_buffer_t));
            if (cap == NULL) {
                LOG_ERROR("Failed to alloc capture stats buffer for stream %p.", (void *)stm);
                cap = NULL;
            } else if (hashmap_put(capture_stats_map, (void *)stm, (void *)cap, false) != 0) {
                LOG_WARN("Failed to insert capture stats entry for stream %p, group not tracked.", (void *)stm);
                free(cap);
                cap = NULL;
            }
        }
        if (cap != NULL) {
            if (cap->grp_count < MAX_GRPS_PER_CAPTURE) {
                cap->grp_handles[cap->grp_count++] = handle;
            } else {
                LOG_WARN("Group handles overflow (max %d) for stream %p, group %p not tracked.", MAX_GRPS_PER_CAPTURE,
                         (void *)stm, (void *)handle);
            }
            cap->count += st->tmp_count;
        }
    }
    st->mode = 0;
    st->tmp_block_dim = 0;
    st->tmp_count = 0;
    pthread_mutex_unlock(&g_stats_map_mutex);
}

void task_update_begin(rtStream_t stm, rtTaskGrp_t handle)
{
    pthread_mutex_lock(&g_stats_map_mutex);
    task_grp_state_t *st = task_grp_state_reset(stm);
    if (st == NULL) {
        pthread_mutex_unlock(&g_stats_map_mutex);
        return;
    }
    st->mode = 2; /* IN_UPDATE */
    st->update_handle = handle;
    pthread_mutex_unlock(&g_stats_map_mutex);
}

void task_update_end(rtStream_t stm)
{
    pthread_mutex_lock(&g_stats_map_mutex);
    task_grp_state_t *st = task_grp_state_get(stm);
    if (st == NULL || st->mode != 2) {
        pthread_mutex_unlock(&g_stats_map_mutex);
        return;
    }
    /* 用 Update 区间的累加值刷新 group */
    task_group_map_set(st->update_handle, st->tmp_block_dim);
    st->mode = 0;
    st->update_handle = NULL;
    st->tmp_block_dim = 0;
    st->tmp_count = 0;
    pthread_mutex_unlock(&g_stats_map_mutex);
}

void launch_stats_dispatch(rtStream_t stm, uint32_t block_dim)
{
    pthread_mutex_lock(&g_stats_map_mutex);
    /* 1. TaskUpdate / TaskGrp 区间内的 kernellaunch 优先归属 task group */
    task_grp_state_t *st = task_grp_state_get(stm);
    if (st != NULL && st->mode != 0) {
        st->tmp_block_dim += block_dim;
        st->tmp_count += 1;
        pthread_mutex_unlock(&g_stats_map_mutex);
        return;
    }
    pthread_mutex_unlock(&g_stats_map_mutex);
    /* 2. 图捕获中的算子. capture_stats_add 内部持锁. */
    if (stream_is_capturing(stm)) {
        capture_stats_add(stm, block_dim);
        return;
    }
    /* 3. 单算子 */
    vnpu_stats_record(get_vnpu_id(), block_dim, 1ULL);
}
