/* Copyright (c) Huawei Technologies Co., Ltd. 2026. All rights reserved.
 * Licensed under Mulan PSL v2. */
#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif
#include "stream_drain.h"
#include "common.h"
#include "core_limiter.h"
#include "runtime_hook.h"

extern volatile int g_terminate;
extern atomic_bool g_sched_locking;
extern uint8_t g_vnpu_id;
extern vnpu_time_slice_sched_t *g_vnpu_sched_context;
static pthread_mutex_t drain_mutex = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t drain_cond = PTHREAD_COND_INITIALIZER;
static bool started, pending;
static bool succeeded = true;
static rtStream_t current_stream;
static cache_streams_t batch;
static uint64_t batch_turn;

static void *stream_drain_worker(void *unused)
{
    (void)unused;
    (void)pthread_setname_np(pthread_self(), "enpu-drain");
    pthread_mutex_lock(&drain_mutex);
    for (;;) {
        while (!pending) {
            pthread_cond_wait(&drain_cond, &drain_mutex);
        }
        uint64_t turn = batch_turn;
        pthread_mutex_unlock(&drain_mutex);
        bool success = true;
        LOG_DEBUG("[DRAIN] batch begin vnpu=%u turn=%llu streams=%d models=%d", g_vnpu_id, (unsigned long long)turn,
                  batch.num_streams, atomic_load(&hasModelExecuteSync));
        /* Model execution can itself need nested launch hooks. No admission or
         * shared scheduler lock is held while waiting here. */
        while (!g_terminate && atomic_load(&hasModelExecuteSync) > 0) {
            ns_sleep(WAITING_SLEEP_PERIOD);
        }
        for (int i = 0; !g_terminate && i < batch.num_streams; ++i) {
            pthread_mutex_lock(&drain_mutex);
            current_stream = batch.streams[i];
            pthread_mutex_unlock(&drain_mutex);
            uint64_t begin = ns_now();
            LOG_DEBUG("[DRAIN] enter owner=%d vnpu=%u turn=%llu stream=%p locking=%d expected=%d completed=%d",
                      atomic_load(&g_vnpu_sched_context->owner), g_vnpu_id, (unsigned long long)turn, batch.streams[i],
                      atomic_load(&g_sched_locking), atomic_load(&g_vnpu_sched_context->vnpu_sync_expected[g_vnpu_id]),
                      atomic_load(&g_vnpu_sched_context->vnpu_sync_completed[g_vnpu_id]));
            rtError_t result = RUNTIME_HOOK_CALL(rt_library_entry, rtStreamSynchronize, batch.streams[i]);
            LOG_DEBUG("[DRAIN] exit owner=%d vnpu=%u turn=%llu stream=%p locking=%d result=%d elapsed_ns=%llu",
                      atomic_load(&g_vnpu_sched_context->owner), g_vnpu_id, (unsigned long long)turn, batch.streams[i],
                      atomic_load(&g_sched_locking), result, (unsigned long long)(ns_now() - begin));
            if (result != ACL_RT_SUCCESS) {
                success = false;
                LOG_WARN("[DRAIN] Runtime sync failed: vnpu=%u turn=%llu stream=%p result=%d", g_vnpu_id,
                         (unsigned long long)turn, batch.streams[i], result);
            }
        }
        pthread_mutex_lock(&drain_mutex);
        current_stream = NULL;
        succeeded = success && !g_terminate;
        pending = false;
        /* Only the scheduler publishes shared completion/debt, never this late worker. */
    }
    return NULL;
}

int stream_drain_start(void)
{
    pthread_mutex_lock(&drain_mutex);
    int rc = 0;
    if (!started) {
        pthread_t thread;
        rc = pthread_create(&thread, NULL, stream_drain_worker, NULL);
        if (rc == 0) {
            pthread_detach(thread);
            started = true;
        }
    }
    pthread_mutex_unlock(&drain_mutex);
    return rc;
}

bool stream_drain_submit(const rtStream_t *streams, int count, uint64_t turn)
{
    pthread_mutex_lock(&drain_mutex);
    bool accepted = started && !pending && count >= 0 && count <= MAX_STREAMS_PER_PROCESS;
    if (accepted) {
        if (count > 0 && memcpy_s(batch.streams, sizeof(batch.streams), streams, count * sizeof(*streams)) != 0) {
            pthread_mutex_unlock(&drain_mutex);
            return false;
        }
        batch.num_streams = count;
        batch_turn = turn;
        pending = true;
        pthread_cond_signal(&drain_cond);
    }
    pthread_mutex_unlock(&drain_mutex);
    return accepted;
}

bool stream_drain_finished(void)
{
    pthread_mutex_lock(&drain_mutex);
    bool finished = !pending;
    pthread_mutex_unlock(&drain_mutex);
    return finished;
}

bool stream_drain_succeeded(void)
{
    pthread_mutex_lock(&drain_mutex);
    bool success = !pending && succeeded;
    pthread_mutex_unlock(&drain_mutex);
    return success;
}

rtStream_t stream_drain_current_stream(void)
{
    pthread_mutex_lock(&drain_mutex);
    rtStream_t stream = current_stream;
    pthread_mutex_unlock(&drain_mutex);
    return stream;
}

bool stream_drain_uses(rtStream_t stream)
{
    pthread_mutex_lock(&drain_mutex);
    bool used = false;
    for (int i = 0; pending && i < batch.num_streams; ++i) {
        if (batch.streams[i] == stream) {
            used = true;
            break;
        }
    }
    pthread_mutex_unlock(&drain_mutex);
    return used;
}

bool stream_drain_wait_stream(rtStream_t stream)
{
    uint64_t deadline = ns_now() + SYNC_WAIT_TIMEOUT_NS;
    while (stream_drain_uses(stream)) {
        if (g_terminate || ns_now() >= deadline) {
            LOG_WARN("[DRAIN] retain stream=%p: worker still holds handle; lifecycle operation must be retried",
                     stream);
            return false;
        }
        ns_sleep(WAITING_SLEEP_PERIOD);
    }
    return true;
}
