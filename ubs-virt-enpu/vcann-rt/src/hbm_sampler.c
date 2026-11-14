/* Copyright (c) Huawei Technologies Co., Ltd. 2026. All rights reserved.
 * Licensed under Mulan PSL v2.
 */
#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif
#include "hbm_sampler.h"
#include <errno.h>
#include <fcntl.h>
#include <pthread.h>
#include <sys/mman.h>
#include <sys/random.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>
#include "config.h"
#include "core_limiter.h"
#include "dcmi_wrapper.h"
#include "npu_manager.h"
#include "utils.h"
#include "vnpu_stats.h"
#include "vnpu_stats_hbm.h"

_Static_assert(ATOMIC_INT_LOCK_FREE == 2 && ATOMIC_LONG_LOCK_FREE == 2 && ATOMIC_LLONG_LOCK_FREE == 2,
               "HBM shared atomics must be lock free across processes");

static pthread_mutex_t g_hbm_mutex = PTHREAD_MUTEX_INITIALIZER;
/* Fork must not wait on a mutex held while logging: the log consumer itself
 * forks for compression. This small mutex protects descriptor transitions only. */
static pthread_mutex_t g_hbm_fd_mutex = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t g_hbm_wakeup;
static pthread_cond_t g_hbm_state_changed = PTHREAD_COND_INITIALIZER;
static pthread_t g_hbm_thread;
static bool g_hbm_started;
static bool g_hbm_stop;
static bool g_hbm_stopping;
static bool g_hbm_fork_child;
static int g_hbm_fd = -1;
static uint64_t g_generation;
static uint64_t g_instance;
static vnpu_stats_hbm_slot_t *g_slot;
static bool g_atfork_registered;

static void hbm_fork_prepare(void)
{
    pthread_mutex_lock(&g_hbm_fd_mutex);
}
static void hbm_fork_parent(void)
{
    pthread_mutex_unlock(&g_hbm_fd_mutex);
}
static void hbm_fork_child(void)
{
    /* Do not keep a parent's OFD ownership alive after its exit. Starting new
     * threads from an atfork handler is unsafe; fork children must exec before
     * using this sampler (same as a fresh LD_PRELOAD workload initialization). */
    if (g_hbm_fd >= 0) {
        close(g_hbm_fd);
    }
    g_hbm_fd = -1;
    g_hbm_started = false;
    g_hbm_stopping = false;
    g_hbm_fork_child = true;
    pthread_mutex_unlock(&g_hbm_fd_mutex);
}

static bool wait_next_sample(void)
{
    struct timespec deadline;
    if (clock_gettime(CLOCK_MONOTONIC, &deadline) != 0) {
        LOG_ERROR("[HBM-STATS] Sample clock failed: errno=%d", errno);
        return false;
    }
    deadline.tv_sec += VNPU_STATS_HBM_PERIOD_NS / NS_PER_S;
    deadline.tv_nsec += VNPU_STATS_HBM_PERIOD_NS % NS_PER_S;
    if (deadline.tv_nsec >= (long)NS_PER_S) {
        deadline.tv_sec++;
        deadline.tv_nsec -= (long)NS_PER_S;
    }
    pthread_mutex_lock(&g_hbm_mutex);
    int ret = 0;
    while (!g_hbm_stop && ret == 0) {
        ret = pthread_cond_timedwait(&g_hbm_wakeup, &g_hbm_mutex, &deadline);
    }
    bool running = !g_hbm_stop && (ret == 0 || ret == ETIMEDOUT);
    pthread_mutex_unlock(&g_hbm_mutex);
    if (ret != 0 && ret != ETIMEDOUT) {
        LOG_ERROR("[HBM-STATS] Sample wait failed: ret=%d", ret);
    }
    return running;
}

static void *hbm_sample_loop(void *unused)
{
    (void)unused;
    uint8_t vnpu = get_vnpu_id();
    bool owner = false;
    vnpu_hbm_snapshot_t sample = {.generation = g_generation, .producer_instance = g_instance};
    struct flock lock = {.l_type = F_WRLCK, .l_whence = SEEK_SET, .l_start = (off_t)vnpu + 1, .l_len = 1};
    int last_error = 0;
    uint64_t last_warning = 0;
    do {
        if (atomic_load(&g_slot->requested_generation) != g_generation) {
            LOG_INFO("[HBM-STATS] Retiring superseded sampler: vnpu=%u generation=%llu", vnpu,
                     (unsigned long long)g_generation);
            break;
        }
        if (!owner) {
            if (fcntl(g_hbm_fd, F_OFD_SETLK, &lock) != 0) {
                if (errno != EACCES && errno != EAGAIN && errno != EINTR) {
                    LOG_ERROR("[HBM-STATS] OFD ownership lock failed: vnpu=%u errno=%d", vnpu, errno);
                    break;
                }
                continue; /* Wait once per cycle; standby never queries DCMI. */
            }
            owner = true;
            sample.owner_epoch = atomic_load(&g_slot->owner_epoch) + 1;
            LOG_INFO("[HBM-STATS] Collector acquired: vnpu=%u generation=%llu instance=%llu epoch=%llu", vnpu,
                     (unsigned long long)g_generation, (unsigned long long)g_instance,
                     (unsigned long long)sample.owner_epoch);
        }
        uint64_t used = 0;
        int ret = enpu_dcmi_query_hbm_usage(get_logic_id(), get_card_id(), get_device_id(), get_soc_version(), &used);
        uint64_t now = ns_now();
        if (now == 0 && ret == 0) {
            ret = -EIO;
        }
        /* An allocation may be replaced during a slow DCMI call. Never publish
         * the old allocation's result after noticing its replacement. */
        if (atomic_load(&g_slot->requested_generation) != g_generation) {
            break;
        }
        sample.attempted_at_ns = now;
        sample.source_error = ret;
        sample.status = ret == 0 && now != 0 ? VNPU_STATS_HBM_OK : VNPU_STATS_HBM_ERROR;
        if (sample.status == VNPU_STATS_HBM_OK) {
            sample.used_bytes = used;
            sample.sampled_at_ns = now;
            if (last_error != 0) {
                LOG_INFO("[HBM-STATS] Collection recovered: vnpu=%u used_bytes=%llu", vnpu, (unsigned long long)used);
            }
        } else if (ret != last_error || now - last_warning >= 30ULL * NS_PER_S) {
            LOG_WARN(
                "[HBM-STATS] Collection failed: vnpu=%u logic=%d card=%d device=%d ret=%d; old sample not current.",
                vnpu, get_logic_id(), get_card_id(), get_device_id(), ret);
            last_warning = now;
        }
        last_error = ret;
        vnpu_hbm_publish(g_slot, &sample);
        LOG_DEBUG("[HBM-STATS] Published: vnpu=%u generation=%llu used=%llu status=%u ret=%d sampled_ns=%llu", vnpu,
                  (unsigned long long)g_generation, (unsigned long long)sample.used_bytes, sample.status, ret,
                  (unsigned long long)sample.sampled_at_ns);
    } while (wait_next_sample());
    if (owner) {
        lock.l_type = F_UNLCK;
        (void)fcntl(g_hbm_fd, F_OFD_SETLK, &lock);
    }
    LOG_INFO("[HBM-STATS] Collector thread stopped: vnpu=%u", vnpu);
    return NULL;
}

int hbm_sampler_start(void)
{
    if (g_hbm_fork_child) {
        return ENPU_FAIL; /* No inherited pthread state is reusable before exec. */
    }
    pthread_mutex_lock(&g_hbm_mutex);
    if (g_hbm_stopping) {
        pthread_mutex_unlock(&g_hbm_mutex);
        return ENPU_FAIL;
    }
    if (g_hbm_started) {
        pthread_mutex_unlock(&g_hbm_mutex);
        return ENPU_SUCCESS;
    }
    vnpu_stats_shm_t *shm = vnpu_stats_shared();
    if (g_hbm_fork_child || shm == NULL || config.resource_generation == 0 || get_vnpu_id() >= MAX_VNPU) {
        LOG_WARN(
            "[HBM-STATS] Sampler disabled: stats=%d generation=%llu fork_child=%d; regenerate config/exec required.",
            shm != NULL, (unsigned long long)config.resource_generation, g_hbm_fork_child);
        goto fail;
    }
    if (!g_atfork_registered) {
        int fork_ret = pthread_atfork(hbm_fork_prepare, hbm_fork_parent, hbm_fork_child);
        if (fork_ret != 0) {
            LOG_ERROR("[HBM-STATS] Failed to register fork cleanup: ret=%d", fork_ret);
            goto fail;
        }
        g_atfork_registered = true;
    }
    if (getrandom(&g_instance, sizeof(g_instance), 0) != (ssize_t)sizeof(g_instance) || g_instance == 0) {
        LOG_ERROR("[HBM-STATS] Failed to create unique producer instance: errno=%d", errno);
        goto fail;
    }
    char name[SHM_ID_LEN + sizeof(VNPU_STATS_SHM_ID_SUFFIX)];
    if (snprintf_s(name, sizeof(name), sizeof(name) - 1, "%s%s", get_vnpu_shm_id(), VNPU_STATS_SHM_ID_SUFFIX) < 0) {
        goto fail;
    }
    /* A separate open-file description owns the OFD lock. Closing the ordinary
     * stats fd or a monitor's mapping cannot accidentally release this lock. */
    pthread_mutex_lock(&g_hbm_fd_mutex);
    g_hbm_fd = shm_open(name, O_RDWR | O_CLOEXEC, 0);
    pthread_mutex_unlock(&g_hbm_fd_mutex);
    struct stat mapped, opened;
    if (g_hbm_fd < 0 || fstat(g_hbm_fd, &opened) != 0 || fstat(vnpu_stats_shared_fd(), &mapped) != 0 ||
        mapped.st_ino != opened.st_ino || mapped.st_dev != opened.st_dev) {
        LOG_ERROR("[HBM-STATS] Stats object changed or cannot open collector fd: errno=%d", errno);
        goto fail;
    }
    g_slot = &shm->hbm[get_vnpu_id()];
    g_generation = config.resource_generation;
    uint64_t requested = atomic_load(&g_slot->requested_generation);
    while (requested < g_generation &&
           !atomic_compare_exchange_weak(&g_slot->requested_generation, &requested, g_generation)) {}
    if (requested > g_generation) {
        LOG_WARN("[HBM-STATS] Refusing old allocation generation=%llu newer=%llu", (unsigned long long)g_generation,
                 (unsigned long long)requested);
        goto fail;
    }
    pthread_condattr_t attr;
    if (pthread_condattr_init(&attr) != 0) {
        LOG_ERROR("[HBM-STATS] Failed to initialize condition attributes.");
        goto fail;
    }
    int ret = pthread_condattr_setclock(&attr, CLOCK_MONOTONIC);
    if (ret == 0) {
        ret = pthread_cond_init(&g_hbm_wakeup, &attr);
    }
    pthread_condattr_destroy(&attr);
    if (ret != 0) {
        LOG_ERROR("[HBM-STATS] Failed to initialize monotonic sample condition: ret=%d", ret);
        goto fail;
    }
    g_hbm_stop = false;
    ret = pthread_create(&g_hbm_thread, NULL, hbm_sample_loop, NULL);
    if (ret != 0) {
        LOG_ERROR("[HBM-STATS] Thread creation failed: ret=%d", ret);
        pthread_cond_destroy(&g_hbm_wakeup);
        goto fail;
    }
    g_hbm_started = true;
    LOG_INFO("[HBM-STATS] Sampler started: vnpu=%u period_ms=%llu generation=%llu", get_vnpu_id(),
             (unsigned long long)(VNPU_STATS_HBM_PERIOD_NS / VNPU_STATS_NS_PER_MS), (unsigned long long)g_generation);
    pthread_mutex_unlock(&g_hbm_mutex);
    return ENPU_SUCCESS;
fail:
    pthread_mutex_lock(&g_hbm_fd_mutex);
    if (g_hbm_fd >= 0) {
        close(g_hbm_fd);
        g_hbm_fd = -1;
    }
    pthread_mutex_unlock(&g_hbm_fd_mutex);
    pthread_mutex_unlock(&g_hbm_mutex);
    return ENPU_FAIL;
}

void hbm_sampler_stop(void)
{
    if (g_hbm_fork_child) {
        return;
    }
    pthread_mutex_lock(&g_hbm_mutex);
    if (g_hbm_stopping) {
        while (g_hbm_stopping) {
            pthread_cond_wait(&g_hbm_state_changed, &g_hbm_mutex);
        }
        pthread_mutex_unlock(&g_hbm_mutex);
        return;
    }
    if (!g_hbm_started) {
        pthread_mutex_unlock(&g_hbm_mutex);
        return;
    }
    g_hbm_stop = true;
    g_hbm_stopping = true;
    pthread_cond_signal(&g_hbm_wakeup);
    pthread_mutex_unlock(&g_hbm_mutex);
    /* No cancellation of DCMI: its stack/buffers must stay alive until it returns. */
    int ret = pthread_join(g_hbm_thread, NULL);
    pthread_mutex_lock(&g_hbm_mutex);
    if (ret != 0) {
        LOG_ERROR("[HBM-STATS] Cannot join collector safely: ret=%d", ret);
        g_hbm_stopping = false;
        pthread_cond_broadcast(&g_hbm_state_changed);
        pthread_mutex_unlock(&g_hbm_mutex);
        return;
    }
    pthread_mutex_lock(&g_hbm_fd_mutex);
    close(g_hbm_fd);
    g_hbm_fd = -1;
    pthread_mutex_unlock(&g_hbm_fd_mutex);
    g_hbm_started = false;
    g_hbm_stopping = false;
    pthread_cond_destroy(&g_hbm_wakeup);
    pthread_cond_broadcast(&g_hbm_state_changed);
    pthread_mutex_unlock(&g_hbm_mutex);
}

__attribute__((destructor)) static void stop_hbm_sampler(void)
{
    hbm_sampler_stop();
}
