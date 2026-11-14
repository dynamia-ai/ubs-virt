/*
 * Copyright (c) Huawei Technologies Co., Ltd. 2025-2026. All rights reserved.
 * enpu-manager is licensed under Mulan PSL v2.
 */

#include "swap_resolver_thread.h"
#include <pthread.h>
#include <stdatomic.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include "log.h"
#include "shm_manager.h"
#include "swap_resolver.h"

struct swap_resolver_thread {
    swap_resolver_t *resolver;
    int phy_id;
    uint64_t poll_interval_ms;
    pthread_t thread;
    atomic_bool running;
};

static void *swap_resolver_thread_func(void *arg)
{
    swap_resolver_thread_t *thread = (swap_resolver_thread_t *)arg;

    LOG_INFO("swap_resolver_thread started for phy_id=%d", thread->phy_id);
    atomic_store(&thread->running, true);
    while (atomic_load(&thread->running)) {
        if (swap_resolver_is_swap_pending(thread->resolver, thread->phy_id)) {
            usleep(thread->poll_interval_ms * 1000);
            continue;
        }

        int ret = swap_resolver_check_and_swap_in(thread->resolver, thread->phy_id);
        if (ret != ENPU_SUCCESS) {
            LOG_ERROR("swap_resolver_thread: check_and_swap_in failed, ret=%d.", ret);
        }

        watermark_level_t level = swap_resolver_check_watermark(thread->resolver, thread->phy_id);

        uint8_t to_swap_out = SWAP_OUT_NONE;
        ret = swap_resolver_check_enpu_swap_out(thread->resolver, thread->phy_id, &to_swap_out);
        if (ret != ENPU_SUCCESS) {
            LOG_ERROR("swap_resolver_thread: check_enpu_swap_out failed, ret=%d.", ret);
        }
        if (level == WATERMARK_PRE_SWAP || to_swap_out != SWAP_OUT_NONE) {
            LOG_INFO("swap_resolver_thread: watermark level=%d detected", level);

            swap_candidate_t candidates[VNPU_SCHED_MAX_VNPU];
            int actual_count = 0;

            ret = swap_resolver_get_idle_models(thread->resolver, thread->phy_id, candidates, VNPU_SCHED_MAX_VNPU,
                                                &actual_count, to_swap_out);
            if (ret != ENPU_SUCCESS || actual_count <= 0) {
                LOG_ERROR("swap_resolver_thread: get_idle_models failed or no idle models, "
                          "ret=%d, count=%d",
                          ret, actual_count);
                usleep(thread->poll_interval_ms * 1000);
                continue;
            }

            swap_candidate_t selected;
            ret = swap_resolver_select_candidate_from_list(thread->resolver, candidates, actual_count, &selected);
            if (ret != ENPU_SUCCESS) {
                LOG_ERROR("swap_resolver_thread: select_candidate failed, ret=%d", ret);
                usleep(thread->poll_interval_ms * 1000);
                continue;
            }

            ret = swap_resolver_write_swap_command(thread->resolver, thread->phy_id, "pod-idle", selected.vnpu_id,
                                                   SWAP_ACTION_OUT, to_swap_out);
            if (ret != ENPU_SUCCESS) {
                LOG_ERROR("swap_resolver_thread: write_swap_command failed, ret=%d", ret);
                usleep(thread->poll_interval_ms * 1000);
                continue;
            }

            LOG_INFO("swap_resolver_thread: swap_cmd OUT written for vnpu_id=%d", selected.vnpu_id);
        }

        usleep(thread->poll_interval_ms * 1000);
    }

    LOG_INFO("swap_resolver_thread stopped for die_id=%d", thread->phy_id);
    return NULL;
}

swap_resolver_thread_t *swap_resolver_thread_create(void *resolver, int phy_id, uint64_t poll_interval_ms)
{
    if (resolver == NULL) {
        LOG_ERROR("swap_resolver_thread_create failed: resolver is NULL");
        return NULL;
    }

    if (phy_id < 0 || poll_interval_ms == 0) {
        LOG_ERROR("swap_resolver_thread_create failed: invalid phy_id=%d or poll_interval_ms=%lu", phy_id,
                  poll_interval_ms);
        return NULL;
    }

    swap_resolver_thread_t *thread = (swap_resolver_thread_t *)calloc(1, sizeof(swap_resolver_thread_t));
    if (thread == NULL) {
        LOG_ERROR("swap_resolver_thread_create failed: calloc returned NULL");
        return NULL;
    }

    thread->resolver = (swap_resolver_t *)resolver;
    thread->phy_id = phy_id;
    thread->poll_interval_ms = poll_interval_ms;
    atomic_store(&thread->running, false);

    return thread;
}

int swap_resolver_thread_destroy(swap_resolver_thread_t *thread)
{
    if (thread == NULL) {
        return ENPU_FAIL;
    }

    free(thread);
    return ENPU_SUCCESS;
}

int swap_resolver_thread_start(swap_resolver_thread_t *thread)
{
    if (thread == NULL) {
        LOG_ERROR("swap_resolver_thread_start failed: thread is NULL");
        return ENPU_FAIL;
    }

    atomic_store(&thread->running, true);

    int ret = pthread_create(&thread->thread, NULL, swap_resolver_thread_func, thread);
    if (ret != 0) {
        LOG_ERROR("swap_resolver_thread_start failed: pthread_create returned %d", ret);
        atomic_store(&thread->running, false);
        return ENPU_FAIL;
    }

    return ENPU_SUCCESS;
}

int swap_resolver_thread_stop(swap_resolver_thread_t *thread)
{
    if (thread == NULL) {
        return ENPU_FAIL;
    }

    atomic_store(&thread->running, false);
    pthread_join(thread->thread, NULL);

    return ENPU_SUCCESS;
}
