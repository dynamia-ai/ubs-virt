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

#include "vnpu_stats.h"

#include <fcntl.h>
#include <limits.h>
#include <stdbool.h>
#include <stdint.h>
#include <unistd.h>

#include "common.h"
#include "core_limiter.h"
#include "log.h"
#include "runtime_hook.h"
#include "utils.h"

static vnpu_stats_shm_t *g_vnpu_stats_shm = NULL;
/* Non-static on purpose: UT resets it to drive vnpu_stats_init error paths (same convention as
 * core_limiter.c globals inspected by tests). */
bool g_vnpu_stats_inited = false;
static atomic_int_least32_t g_my_avg_slot;

atomic_bool g_sampling = false;
pthread_mutex_t g_sampling_mutex;
pthread_mutex_t g_sampling_records_mutex;
atomic_int sample_time = 0;
atomic_int ker_ave_exe_time = 0;
SampleRecordNode *sample_record_head;

int get_kernel_count(rtModel_t mdl)
{
    // get stream of mdl
    uint32_t num_streams = 0;
    int ret = RUNTIME_HOOK_CALL(rt_library_entry, rtModelGetStreams, mdl, NULL, &num_streams);
    if (ret != ACL_RT_SUCCESS || num_streams == 0 || num_streams > MAX_STREAMS_PER_PROCESS) {
        return -1;
    }
    rtStream_t capture_streams[num_streams];
    ret = RUNTIME_HOOK_CALL(rt_library_entry, rtModelGetStreams, mdl, capture_streams, &num_streams);
    if (ret != ACL_RT_SUCCESS) {
        return -1;
    }

    // get tasks of mdl
    int count = 0;
    for (uint32_t i = 0; i < num_streams; ++i) {
        uint32_t num_tasks = 0;
        ret = RUNTIME_HOOK_CALL(rt_library_entry, rtStreamGetTasks, capture_streams[i], NULL, &num_tasks);
        if (ret != ACL_RT_SUCCESS) {
            return -1;
        }
        count += (int)num_tasks;
    }

    return count == 0 ? -1 : count;
}

// 获取并复用随机设备文件描述符，避免每次调用都打开文件
int get_random_fd(void)
{
    static int fd = -1;
    if (fd == -1) {
        fd = open("/dev/urandom", O_RDONLY | O_CLOEXEC);
    }
    return fd;
}

bool is_random_sampling(void)
{
    if (!g_vnpu_stats_inited || !get_vnpu_stats_enable()) {
        return false;
    }

    int fd = get_random_fd();
    if (fd < 0) {
        return false;
    }

    uint32_t rand_val = 0;
    // 读取 4 个字节的随机数
    ssize_t bytes_read = read(fd, &rand_val, sizeof(rand_val));
    if (bytes_read != sizeof(rand_val)) {
        return false;
    }

    // rand_val < (UINT32_MAX / 5) 等价于 20% 的概率
    return rand_val < (UINT32_MAX / 5);
}

int is_stream_capture(rtStream_t stm, rtStreamCaptureStatus *status)
{
    rtModel_t capture_model;
    int ret = RUNTIME_HOOK_CALL(rt_library_entry, rtStreamGetCaptureInfo, stm, status, &capture_model);
    if (ret != ACL_RT_SUCCESS) {
        LOG_WARN("Get stream status failed, ret=%d.", ret);
        return ENPU_FAIL;
    }
    return ENPU_SUCCESS;
}

rtEvent_t g_sample_start_event = NULL;
rtEvent_t g_sample_end_event = NULL;
rtStream_t g_sample_stream = NULL;
int g_sample_sync_count = 0;
bool g_sample_flag = false;

void sampling_begin(rtStream_t stm)
{
    pthread_mutex_lock(&g_sampling_mutex);
    atomic_store(&g_sampling, true);

    // when stm status is capture, dont't sample
    rtStreamCaptureStatus status;
    int ret = is_stream_capture(stm, &status);
    if (ret != ENPU_SUCCESS) {
        atomic_store(&g_sampling, false);
        return;
    }
    if (status != RT_STREAM_CAPTURE_STATUS_NONE) {
        LOG_DEBUG("Stream is captured, skip sample.");
        atomic_store(&g_sampling, false);
        return;
    }

    if (g_sample_flag) {
        atomic_store(&g_sampling, false);
        return;
    }
    g_sample_flag = true;
    g_sample_stream = stm;

    ret = RUNTIME_HOOK_CALL(rt_library_entry, rtEventCreateExWithFlag, &g_sample_start_event, ACL_EVENT_TIME_LINE);
    if (ret != ACL_RT_SUCCESS || g_sample_start_event == NULL) {
        LOG_DEBUG("Sample start event create failed, ret=%d.", ret);
        g_sample_start_event = NULL;
        g_sample_stream = NULL;
        g_sample_flag = false;
        atomic_store(&g_sampling, false);
        return;
    }
    ret = RUNTIME_HOOK_CALL(rt_library_entry, rtEventRecord, g_sample_start_event, stm);
    if (ret != ACL_RT_SUCCESS) {
        LOG_DEBUG("Sample start event record failed, ret=%d.", ret);
        RUNTIME_HOOK_CALL(rt_library_entry, rtEventDestroy, g_sample_start_event);
        g_sample_start_event = NULL;
        g_sample_stream = NULL;
        g_sample_flag = false;
        atomic_store(&g_sampling, false);
        return;
    }
}

int add_sample_record(int kernel_count, uint64_t sample_time, uint64_t timestamp)
{
    if (sample_record_head == NULL) {
        LOG_ERROR("Sample record head is NULL.");
        return ENPU_FAIL;
    }

    SampleRecordNode *new_node = (SampleRecordNode *)malloc(sizeof(SampleRecordNode));
    if (!new_node) {
        LOG_ERROR("Add sample record malloc node failed.");
        return ENPU_FAIL;
    }

    new_node->kernel_count = kernel_count;
    new_node->sample_time = sample_time;
    new_node->timestamp = timestamp;

    /* 链表与调度线程的 get_ker_ave_exec_time 并发 */
    pthread_mutex_lock(&g_sampling_records_mutex);
    new_node->next = sample_record_head->next;
    sample_record_head->next = new_node;
    pthread_mutex_unlock(&g_sampling_records_mutex);
    return ENPU_SUCCESS;
}

void *sample_sync(void *args)
{
    (void)args;
    RUNTIME_HOOK_CALL(rt_library_entry, rtStreamSynchronize, g_sample_stream);
    float use_time = 0;
    RUNTIME_HOOK_CALL(rt_library_entry, rtEventElapsedTime, &use_time, g_sample_start_event, g_sample_end_event);
    RUNTIME_HOOK_CALL(rt_library_entry, rtEventDestroy, g_sample_start_event);
    RUNTIME_HOOK_CALL(rt_library_entry, rtEventDestroy, g_sample_end_event);
    g_sample_start_event = NULL;
    g_sample_end_event = NULL;
    g_sample_stream = NULL;
    uint64_t now = ns_now();
    int ret = add_sample_record(g_sample_sync_count, (uint64_t)(use_time * NS_PER_MS), now);
    CHECK_ERROR_CODE(ret, "Add sample record failed.");

    g_sample_flag = false;
    return NULL;
}

void record_event_time(rtStream_t stm, int count)
{
    int ret = RUNTIME_HOOK_CALL(rt_library_entry, rtEventCreateExWithFlag, &g_sample_end_event, ACL_EVENT_TIME_LINE);
    if (ret != ACL_RT_SUCCESS || g_sample_end_event == NULL) {
        LOG_DEBUG("Sample end event create failed, ret=%d.", ret);
        RUNTIME_HOOK_CALL(rt_library_entry, rtEventDestroy, g_sample_start_event);
        g_sample_start_event = NULL;
        /* 出参可能已被写过，清掉以免留下悬垂句柄（与下面 record 失败分支对称）。 */
        g_sample_end_event = NULL;
        g_sample_flag = false;
        return;
    }
    ret = RUNTIME_HOOK_CALL(rt_library_entry, rtEventRecord, g_sample_end_event, stm);
    if (ret != ACL_RT_SUCCESS) {
        LOG_DEBUG("Sample end event record failed, ret=%d.", ret);
        RUNTIME_HOOK_CALL(rt_library_entry, rtEventDestroy, g_sample_start_event);
        g_sample_start_event = NULL;
        RUNTIME_HOOK_CALL(rt_library_entry, rtEventDestroy, g_sample_end_event);
        g_sample_end_event = NULL;
        g_sample_flag = false;
        return;
    }
    g_sample_sync_count = count;
    pthread_t thread;
    int rc = pthread_create(&thread, NULL, sample_sync, NULL);
    if (rc != 0) {
        LOG_ERROR("Failed to create sample_sync thread, rc=%d.", rc);
        RUNTIME_HOOK_CALL(rt_library_entry, rtEventDestroy, g_sample_start_event);
        g_sample_start_event = NULL;
        RUNTIME_HOOK_CALL(rt_library_entry, rtEventDestroy, g_sample_end_event);
        g_sample_end_event = NULL;
        g_sample_flag = false;
        return;
    }
    pthread_detach(thread);
}

void sampling_end(rtStream_t stm)
{
    if (!atomic_load(&g_sampling)) {
        pthread_mutex_unlock(&g_sampling_mutex);
        return;
    }
    record_event_time(stm, 1);
    atomic_store(&g_sampling, false);
    pthread_mutex_unlock(&g_sampling_mutex);
}

void sampling_graph_end(rtStream_t stm, rtModel_t mdl)
{
    if (!atomic_load(&g_sampling)) {
        pthread_mutex_unlock(&g_sampling_mutex);
        return;
    }
    record_event_time(stm, get_kernel_count(mdl));
    atomic_store(&g_sampling, false);
    pthread_mutex_unlock(&g_sampling_mutex);
}

void remove_sample_records(SampleRecordNode *node)
{
    while (node->next) {
        SampleRecordNode *old_node = node->next;
        node->next = node->next->next;
        free(old_node);
    }
    node->next = NULL;
}

uint64_t get_ker_ave_exec_time(uint64_t *count)
{
    *count = 0;
    if (sample_record_head == NULL) {
        return 0;
    }
    uint64_t now = ns_now();
    uint64_t total_sample_time = 0;
    int total_kernel_count = 0;

    /* 与 sample_sync 线程的 add_sample_record 并发 */
    pthread_mutex_lock(&g_sampling_records_mutex);
    SampleRecordNode *record = sample_record_head;
    while (record->next) {
        SampleRecordNode *last_record = record;
        record = record->next;
        if (now - record->timestamp <= INVALID_SAMPLE_PERIOD) {
            total_kernel_count += record->kernel_count;
            total_sample_time += record->sample_time;
        } else {
            remove_sample_records(last_record);
            break;
        }
    }
    pthread_mutex_unlock(&g_sampling_records_mutex);

    *count = (uint64_t)total_kernel_count;
    return (total_kernel_count == 0) ? 0 : (total_sample_time / (uint64_t)total_kernel_count);
}

static int vnpu_stats_do_init(vnpu_stats_shm_t *shm, bool *is_first_init)
{
    *is_first_init = false;
    uint64_t begin = ns_now();
    while (true) {
        uint32_t magic = atomic_load(&shm->magic_number);
        if (magic == VNPU_STATS_MAGIC_INITIALIZED) {
            return ENPU_SUCCESS;
        }
        if (magic == VNPU_STATS_MAGIC_INITIALIZING) {
            uint64_t now = ns_now();
            if (now - begin > VNPU_STATS_INIT_TIMEOUT_NS) {
                LOG_WARN("Init vnpu stats shm timed out, reclaiming.");
                atomic_store(&shm->magic_number, VNPU_STATS_MAGIC_UNINITIALIZED);
                begin = now;
            }
            ns_sleep(VNPU_STATS_WAIT_SLEEP_NS);
            continue;
        }
        atomic_store(&shm->magic_number, VNPU_STATS_MAGIC_INITIALIZING);
        for (int i = 0; i < MAX_VNPU; ++i) {
            atomic_store(&shm->avg_slot_seq[i], 0);
            for (int s = 0; s < VNPU_STATS_AVG_SLOTS; ++s) {
                atomic_store(&shm->avg_slots[i][s].sum_ns, 0);
                atomic_store(&shm->avg_slots[i][s].count, 0);
                atomic_store(&shm->avg_slots[i][s].ts_ns, 0);
            }
            for (int j = 0; j < VNPU_STATS_NUM_BUCKETS; ++j) {
                atomic_store(&shm->buckets[i][j].bucket_start_ns, 0);
                atomic_store(&shm->buckets[i][j].sum_block_dim, 0);
                atomic_store(&shm->buckets[i][j].launch_count, 0);
            }
        }
        atomic_store(&shm->magic_number, VNPU_STATS_MAGIC_INITIALIZED);
        *is_first_init = true;
        return ENPU_SUCCESS;
    }
}

int vnpu_stats_init(const char *base_shm_id)
{
    if (g_vnpu_stats_inited) {
        return ENPU_SUCCESS;
    }
    if (base_shm_id == NULL || base_shm_id[0] == '\0') {
        LOG_ERROR("Failed to init vnpu stats: base_shm_id is null or empty.");
        return ENPU_FAIL;
    }

    int ret = pthread_mutex_init(&g_sampling_mutex, NULL);
    if (ret != 0) {
        LOG_ERROR("Failed to init g_sampling_mutex, error=%d.", ret);
        return ENPU_FAIL;
    }
    ret = pthread_mutex_init(&g_sampling_records_mutex, NULL);
    if (ret != 0) {
        LOG_ERROR("Failed to init g_sampling_records_mutex, error=%d.", ret);
        pthread_mutex_destroy(&g_sampling_mutex);
        return ENPU_FAIL;
    }
    sample_record_head = (SampleRecordNode *)malloc(sizeof(SampleRecordNode));
    if (!sample_record_head) {
        LOG_ERROR("Sample record malloc head node failed.");
        pthread_mutex_destroy(&g_sampling_records_mutex);
        pthread_mutex_destroy(&g_sampling_mutex);
        return ENPU_FAIL;
    }
    sample_record_head->next = NULL;

    char stats_shm_id[SHM_ID_LEN + 8] = {0};
    int rc = snprintf_s(stats_shm_id, sizeof(stats_shm_id), sizeof(stats_shm_id) - 1, "%s%s", base_shm_id,
                        VNPU_STATS_SHM_ID_SUFFIX);
    if (rc < 0 || (size_t)rc >= sizeof(stats_shm_id)) {
        LOG_ERROR("Failed to derive vnpu stats shm id, base=%s.", base_shm_id);
        free(sample_record_head);
        sample_record_head = NULL;
        pthread_mutex_destroy(&g_sampling_records_mutex);
        pthread_mutex_destroy(&g_sampling_mutex);
        return ENPU_FAIL;
    }

    vnpu_stats_shm_t *shm = (vnpu_stats_shm_t *)map_share_mem(stats_shm_id, sizeof(vnpu_stats_shm_t));
    if (shm == NULL) {
        LOG_ERROR("Failed to map vnpu stats share memory, id=%s.", stats_shm_id);
        free(sample_record_head);
        sample_record_head = NULL;
        pthread_mutex_destroy(&g_sampling_records_mutex);
        pthread_mutex_destroy(&g_sampling_mutex);
        return ENPU_FAIL;
    }

    bool is_first = false;
    if (vnpu_stats_do_init(shm, &is_first) != ENPU_SUCCESS) {
        LOG_ERROR("Failed to initialize vnpu stats share memory content.");
        unmap_share_mem(shm, sizeof(vnpu_stats_shm_t));
        free(sample_record_head);
        sample_record_head = NULL;
        pthread_mutex_destroy(&g_sampling_records_mutex);
        pthread_mutex_destroy(&g_sampling_mutex);
        return ENPU_FAIL;
    }

    g_vnpu_stats_shm = shm;
    atomic_store(&g_my_avg_slot, -1);
    g_vnpu_stats_inited = true;
    if (is_first) {
        LOG_INFO("Init vnpu stats succeeded (first), shm_id=%s.", stats_shm_id);
    }
    return ENPU_SUCCESS;
}

int64_t vnpu_stats_get_avg_duration_ns(uint8_t vnpu_id)
{
    if (!g_vnpu_stats_inited || g_vnpu_stats_shm == NULL || vnpu_id >= MAX_VNPU) {
        return 0;
    }
    uint64_t now = ns_now();
    uint64_t total_sum = 0;
    uint64_t total_count = 0;
    for (int i = 0; i < VNPU_STATS_AVG_SLOTS; ++i) {
        vnpu_stats_avg_slot_t *slot = &g_vnpu_stats_shm->avg_slots[vnpu_id][i];
        uint64_t ts = atomic_load(&slot->ts_ns);
        if (ts == 0 || now < ts || (now - ts) > VNPU_STATS_AVG_STALE_NS) {
            continue;
        }
        uint64_t count = atomic_load(&slot->count);
        if (count == 0) {
            continue;
        }
        total_sum += atomic_load(&slot->sum_ns);
        total_count += count;
    }
    if (total_count == 0) {
        return 0;
    }
    return (int64_t)(total_sum / total_count);
}

void vnpu_stats_set_local_avg_duration(uint8_t vnpu_id, uint64_t avg_duration_ns, uint64_t count)
{
    if (!g_vnpu_stats_inited || g_vnpu_stats_shm == NULL || vnpu_id >= MAX_VNPU) {
        return;
    }
    if (count == 0) {
        return;
    }

    int32_t my_slot = atomic_load(&g_my_avg_slot);
    if (my_slot < 0) {
        uint64_t seq = atomic_fetch_add(&g_vnpu_stats_shm->avg_slot_seq[vnpu_id], 1ULL);
        my_slot = (int32_t)(seq % VNPU_STATS_AVG_SLOTS);
        atomic_store(&g_my_avg_slot, my_slot);
    }

    vnpu_stats_avg_slot_t *slot = &g_vnpu_stats_shm->avg_slots[vnpu_id][my_slot];
    uint64_t now = ns_now();
    atomic_store(&slot->sum_ns, avg_duration_ns * count);
    atomic_store(&slot->count, count);
    atomic_store(&slot->ts_ns, now);
}

void vnpu_stats_record(uint8_t vnpu_id, uint32_t block_dim, uint64_t count)
{
    if (!g_vnpu_stats_inited || g_vnpu_stats_shm == NULL) {
        return;
    }
    if (vnpu_id >= MAX_VNPU) {
        return;
    }

    uint64_t now = ns_now();
    uint64_t bucket_start = now - (now % VNPU_STATS_BUCKET_DURATION_NS);
    uint32_t idx = (uint32_t)((bucket_start / VNPU_STATS_BUCKET_DURATION_NS) % VNPU_STATS_NUM_BUCKETS);
    vnpu_stats_bucket_t *bucket = &g_vnpu_stats_shm->buckets[vnpu_id][idx];

    uint64_t expected = atomic_load(&bucket->bucket_start_ns);
    if (expected != bucket_start) {
        if (atomic_compare_exchange_strong(&bucket->bucket_start_ns, &expected, bucket_start)) {
            atomic_store(&bucket->sum_block_dim, (uint64_t)block_dim);
            atomic_store(&bucket->launch_count, count);
            return;
        }
    }
    atomic_fetch_add(&bucket->sum_block_dim, (uint64_t)block_dim);
    atomic_fetch_add(&bucket->launch_count, count);
}

int vnpu_stats_query(uint8_t vnpu_id, vnpu_stats_aggregate_t *out)
{
    if (out == NULL) {
        LOG_ERROR("Failed to query vnpu stats: out is null.");
        return ENPU_FAIL;
    }
    out->sum_block_dim = 0;
    out->launch_count = 0;

    if (!g_vnpu_stats_inited || g_vnpu_stats_shm == NULL) {
        return ENPU_FAIL;
    }
    if (vnpu_id >= MAX_VNPU) {
        LOG_ERROR("Failed to query vnpu stats: vnpu_id %u out of range [0, %d).", vnpu_id, MAX_VNPU);
        return ENPU_FAIL;
    }

    uint64_t now = ns_now();
    uint64_t window_start = (now >= VNPU_STATS_WINDOW_NS) ? (now - VNPU_STATS_WINDOW_NS) : 0;

    for (int i = 0; i < VNPU_STATS_NUM_BUCKETS; ++i) {
        const vnpu_stats_bucket_t *b = &g_vnpu_stats_shm->buckets[vnpu_id][i];
        uint64_t bts = atomic_load(&b->bucket_start_ns);
        if (bts == 0 || bts < window_start || bts > now) {
            continue;
        }
        out->sum_block_dim += atomic_load(&b->sum_block_dim);
        out->launch_count += atomic_load(&b->launch_count);
    }
    return ENPU_SUCCESS;
}
