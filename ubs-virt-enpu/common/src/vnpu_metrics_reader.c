/*
 * Copyright (c) Huawei Technologies Co., Ltd. 2026-2026. All rights reserved.
 * ubs-virt-enpu is licensed under Mulan PSL v2.
 * You can use this software according to the terms and conditions of the Mulan PSL v2.
 * You may obtain a copy of the License at:
 *          http://license.coscl.org.cn/MulanPSL2
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND,
 * EITHER EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT,
 * MERCHANTABILITY OR FIT FOR A PARTICULAR PURPOSE.
 * See the Mulan PSL v2 for more details.
 */

#include "vnpu_metrics_reader.h"
#include <errno.h>
#include <fcntl.h>
#include <stdatomic.h>
#include <stdio.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>
#include "log.h"
#include "securec.h"

#define VNPU_STATS_SHM_NAME_LEN 256 /* POSIX shm object name, including its suffix. */

static uint64_t monotonic_ns(void)
{
    struct timespec ts = {0};
    if (clock_gettime(CLOCK_MONOTONIC, &ts) != 0) {
        return 0;
    }
    return (uint64_t)ts.tv_sec * 1000000000ULL + (uint64_t)ts.tv_nsec;
}

static void read_vnpu_metric(const vnpu_stats_shm_t *shm, int vnpu_id, uint64_t now, vnpu_raw_metric_t *metric)
{
    uint64_t window_start = (now >= VNPU_STATS_WINDOW_NS) ? now - VNPU_STATS_WINDOW_NS : 0;
    for (int i = 0; i < VNPU_STATS_NUM_BUCKETS; i++) {
        const vnpu_stats_bucket_t *bucket = &shm->buckets[vnpu_id][i];
        uint64_t bucket_start = atomic_load_explicit(&bucket->bucket_start_ns, memory_order_acquire);
        if (bucket_start == 0 || bucket_start < window_start || bucket_start > now) {
            continue;
        }
        metric->sum_block_dim += atomic_load_explicit(&bucket->sum_block_dim, memory_order_relaxed);
        metric->launch_count += atomic_load_explicit(&bucket->launch_count, memory_order_relaxed);
    }

    uint64_t total_ns = 0;
    uint64_t total_count = 0;
    for (int i = 0; i < VNPU_STATS_AVG_SLOTS; i++) {
        const vnpu_stats_avg_slot_t *slot = &shm->avg_slots[vnpu_id][i];
        uint64_t timestamp = atomic_load_explicit(&slot->ts_ns, memory_order_acquire);
        if (timestamp == 0 || timestamp > now || now - timestamp > VNPU_STATS_AVG_STALE_NS) {
            continue;
        }
        uint64_t count = atomic_load_explicit(&slot->count, memory_order_relaxed);
        if (count == 0) {
            continue;
        }
        total_ns += atomic_load_explicit(&slot->sum_ns, memory_order_relaxed);
        total_count += count;
    }
    metric->avg_kernel_duration_ns = (total_count == 0) ? 0 : total_ns / total_count;
}

vnpu_stats_reader_status_t vnpu_metrics_reader_snapshot(const char *base_shm_id, vnpu_stats_snapshot_t *snapshot)
{
    if (base_shm_id == NULL || base_shm_id[0] == '\0' || snapshot == NULL) {
        return VNPU_STATS_READER_IO_ERROR;
    }

    if (memset_s(snapshot, sizeof(*snapshot), 0, sizeof(*snapshot)) != 0) {
        return VNPU_STATS_READER_IO_ERROR;
    }

    char shm_name[VNPU_STATS_SHM_NAME_LEN] = {0};
    int ret =
        snprintf_s(shm_name, sizeof(shm_name), sizeof(shm_name) - 1, "%s%s", base_shm_id, VNPU_STATS_SHM_ID_SUFFIX);
    if (ret < 0 || (size_t)ret >= sizeof(shm_name)) {
        LOG_ERROR("[VNPU-METRICS] Invalid stats shm name, base=%s", base_shm_id);
        return VNPU_STATS_READER_IO_ERROR;
    }

    int fd = shm_open(shm_name, O_RDONLY | O_CLOEXEC, 0);
    if (fd < 0) {
        if (errno != ENOENT) {
            LOG_WARN("[VNPU-METRICS] Failed to open %s read-only: %s", shm_name, strerror(errno));
            return VNPU_STATS_READER_IO_ERROR;
        }
        return VNPU_STATS_READER_MISSING;
    }

    struct stat stat_buf = {0};
    if (fstat(fd, &stat_buf) != 0) {
        LOG_WARN("[VNPU-METRICS] Stats fstat failed: shm=%s errno=%d", shm_name, errno);
        close(fd);
        return VNPU_STATS_READER_IO_ERROR;
    }
    if (stat_buf.st_size != (off_t)sizeof(vnpu_stats_shm_t)) {
        LOG_WARN("[VNPU-METRICS] Invalid stats shm size, name=%s size=%lld expected=%zu", shm_name,
                 (long long)stat_buf.st_size, sizeof(vnpu_stats_shm_t));
        close(fd);
        return VNPU_STATS_READER_VERSION_MISMATCH;
    }

    const vnpu_stats_shm_t *shm =
        (const vnpu_stats_shm_t *)mmap(NULL, sizeof(vnpu_stats_shm_t), PROT_READ, MAP_SHARED, fd, 0);
    close(fd);
    if (shm == MAP_FAILED) {
        LOG_WARN("[VNPU-METRICS] Failed to mmap %s read-only: %s", shm_name, strerror(errno));
        return VNPU_STATS_READER_IO_ERROR;
    }

    uint32_t magic = atomic_load_explicit(&shm->magic_number, memory_order_acquire);
    if (magic != VNPU_STATS_MAGIC_INITIALIZED || atomic_load(&shm->version) != VNPU_STATS_ABI_VERSION ||
        atomic_load(&shm->struct_size) != sizeof(*shm) || atomic_load(&shm->capacity) != VNPU_STATS_MAX_VNPU) {
        LOG_WARN("[VNPU-METRICS] Stats ABI mismatch: shm=%s magic=0x%x version=%llu size=%llu capacity=%llu", shm_name,
                 magic, (unsigned long long)atomic_load(&shm->version),
                 (unsigned long long)atomic_load(&shm->struct_size), (unsigned long long)atomic_load(&shm->capacity));
        munmap((void *)shm, sizeof(vnpu_stats_shm_t));
        return VNPU_STATS_READER_VERSION_MISMATCH;
    }

    uint64_t now = monotonic_ns();
    if (now == 0) {
        LOG_WARN("[VNPU-METRICS] Failed to read CLOCK_MONOTONIC for shm=%s", shm_name);
        munmap((void *)shm, sizeof(vnpu_stats_shm_t));
        return VNPU_STATS_READER_IO_ERROR;
    }

    snapshot->collected_at_ns = now;
    for (int vnpu_id = 0; vnpu_id < VNPU_STATS_MAX_VNPU; vnpu_id++) {
        read_vnpu_metric(shm, vnpu_id, now, &snapshot->metrics[vnpu_id]);
        snapshot->hbm[vnpu_id] = vnpu_hbm_read(&shm->hbm[vnpu_id]);
        const vnpu_raw_metric_t *metric = &snapshot->metrics[vnpu_id];
        if (metric->launch_count != 0 || metric->avg_kernel_duration_ns != 0) {
            LOG_DEBUG("[VNPU-METRICS] Snapshot: shm=%s vnpu=%d blocks=%llu launches=%llu avg_ns=%llu", shm_name,
                      vnpu_id, (unsigned long long)metric->sum_block_dim, (unsigned long long)metric->launch_count,
                      (unsigned long long)metric->avg_kernel_duration_ns);
        }
    }

    /* HBM may be published during the scan; use its end time for freshness so
     * a newly committed sample is not spuriously rejected as from the future. */
    snapshot->collected_at_ns = monotonic_ns();

    munmap((void *)shm, sizeof(vnpu_stats_shm_t));
    if (snapshot->collected_at_ns == 0) {
        return VNPU_STATS_READER_IO_ERROR;
    }
    return VNPU_STATS_READER_OK;
}
