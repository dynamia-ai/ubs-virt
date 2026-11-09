/*
 * Copyright (c) Huawei Technologies Co., Ltd. 2025-2026. All rights reserved.
 * enpu-manager is licensed under Mulan PSL v2.
 */

#include "../internal/rollback_log.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "../internal/npu_allocator.h"
#include "../internal/npu_allocator_internal.h"
#include "allocation.h"
#include "common.h"
#include "config_manager.h"
#include "enpu_manager.h"
#include "log.h"
#include "npu_tree.h"
#include "securec.h"
#include "shm_manager.h"

#define LOG(fmt, ...) LOG_INFO("[ROLLBACK] " fmt, ##__VA_ARGS__)
#define ERR(fmt, ...) LOG_ERROR("[ROLLBACK] " fmt, ##__VA_ARGS__)

rollback_log_t *rollback_log_create(void)
{
    rollback_log_t *log = (rollback_log_t *)calloc(1, sizeof(rollback_log_t));
    if (!log) {
        ERR("Failed to allocate rollback log");
        return NULL;
    }
    log->step_count = 0;
    LOG("Rollback log created");
    return log;
}

void rollback_log_destroy(rollback_log_t *log)
{
    if (!log)
        return;
    free(log);
    LOG("Rollback log destroyed");
}

int rollback_log_append(rollback_log_t *log, rollback_step_type_t type, int32_t phy_id, int vnpu_id,
                        int32_t aicore_delta, int64_t hbm_delta, int32_t sched_policy, const char *allocation_id,
                        const char *shm_id)
{
    if (!log) {
        ERR("NULL log");
        return ENPU_INVALID_PARAM;
    }

    if (log->step_count >= MAX_STEPS) {
        ERR("Rollback log overflow: step_count=%d, max=%d", log->step_count, MAX_STEPS);
        return ENPU_FAIL;
    }

    rollback_step_t *step = &log->steps[log->step_count];
    step->type = type;
    step->phy_id = phy_id;
    step->vnpu_id = vnpu_id;
    step->aicore_delta = aicore_delta;
    step->hbm_delta = hbm_delta;
    step->sched_policy = sched_policy;

    if (allocation_id) {
        int ret =
            strncpy_s(step->allocation_id, sizeof(step->allocation_id), allocation_id, sizeof(step->allocation_id) - 1);
        CHECK_COND_RETURN_ERROR_CODE(ret != 0, "strncpy_s allocation_id failed.");
    } else {
        step->allocation_id[0] = '\0';
    }

    if (shm_id) {
        int ret = strncpy_s(step->shm_id, sizeof(step->shm_id), shm_id, sizeof(step->shm_id) - 1);
        CHECK_COND_RETURN_ERROR_CODE(ret != 0, "strncpy_s shm_id failed.");
    } else {
        step->shm_id[0] = '\0';
    }

    log->step_count++;
    LOG("Append step %d: type=%d, phy=%d, vnpu=%d", log->step_count - 1, type, phy_id, vnpu_id);
    return ENPU_SUCCESS;
}

static int rollback_mark_occupied(npu_allocator_t *alloc, rollback_step_t *step, npu_node_t *node)
{
    if (node == NULL) {
        ERR("Rollback MARK_OCCUPIED: node not found, phy=%d vnpu=%d", step->phy_id, step->vnpu_id);
        return ENPU_NOT_FOUND;
    }

    int ret = npu_tree_mark_vnpu_free(alloc->tree, node, step->vnpu_id);
    if (ret != ENPU_SUCCESS) {
        ERR("Rollback MARK_OCCUPIED failed: mark_vnpu_free phy=%d vnpu=%d ret=%d", step->phy_id, step->vnpu_id, ret);
        return ret;
    }

    LOG("Rollback MARK_OCCUPIED: mark_vnpu_free phy=%d vnpu=%d", step->phy_id, step->vnpu_id);
    return ENPU_SUCCESS;
}

static int rollback_update_quota(npu_allocator_t *alloc, rollback_step_t *step, npu_node_t *node)
{
    if (node == NULL) {
        ERR("Rollback UPDATE_QUOTA: node not found, phy=%d vnpu=%d", step->phy_id, step->vnpu_id);
        return ENPU_NOT_FOUND;
    }

    int ret = npu_tree_update_quota(alloc->tree, node, -step->aicore_delta, -step->hbm_delta);
    if (ret != ENPU_SUCCESS) {
        ERR("Rollback UPDATE_QUOTA failed: phy=%d vnpu=%d aicore=%d hbm=%ld ret=%d", step->phy_id, step->vnpu_id,
            -step->aicore_delta, -step->hbm_delta, ret);
        return ret;
    }

    LOG("Rollback UPDATE_QUOTA: quota aicore=%d hbm=%ld", -step->aicore_delta, -step->hbm_delta);
    return ENPU_SUCCESS;
}

static int rollback_attach_policy(npu_allocator_t *alloc, rollback_step_t *step, npu_node_t *node)
{
    if (node == NULL) {
        ERR("Rollback ATTACH_POLICY: node not found, phy=%d vnpu=%d", step->phy_id, step->vnpu_id);
        return ENPU_NOT_FOUND;
    }

    int ret = npu_tree_detach_policy(alloc->tree, node, step->vnpu_id);
    if (ret != ENPU_SUCCESS) {
        ERR("Rollback ATTACH_POLICY failed: detach_policy phy=%d vnpu=%d ret=%d", step->phy_id, step->vnpu_id, ret);
        return ret;
    }

    LOG("Rollback ATTACH_POLICY: detach_policy phy=%d vnpu=%d", step->phy_id, step->vnpu_id);
    return ENPU_SUCCESS;
}

static int rollback_create_shm(npu_allocator_t *alloc, rollback_step_t *step)
{
    if (strlen(step->shm_id) == 0) {
        ERR("Rollback CREATE_SHM skipped: empty shm_id, phy=%d vnpu=%d", step->phy_id, step->vnpu_id);
        return ENPU_INVALID_PARAM;
    }

    int ret = shm_manager_remove_entry(alloc->shm_mgr, step->phy_id, step->vnpu_id);
    if (ret != ENPU_SUCCESS) {
        ERR("Rollback CREATE_SHM failed: remove_entry phy=%d vnpu=%d ret=%d", step->phy_id, step->vnpu_id, ret);
        return ret;
    }

    LOG("Rollback CREATE_SHM: remove_entry die=%d vnpu=%d", step->phy_id, step->vnpu_id);
    return ENPU_SUCCESS;
}

static int rollback_write_config(npu_allocator_t *alloc, rollback_step_t *step)
{
    if (!allocation_id_is_valid(step->allocation_id)) {
        ERR("Rollback WRITE_CONFIG skipped: invalid allocation_id, phy=%d vnpu=%d", step->phy_id, step->vnpu_id);
        return ENPU_INVALID_PARAM;
    }

    int ret = config_manager_delete_config(alloc->config_mgr, step->allocation_id);
    if (ret != ENPU_SUCCESS) {
        ERR("Rollback WRITE_CONFIG failed: allocation_id=%s ret=%d", step->allocation_id, ret);
        return ret;
    }

    LOG("Rollback WRITE_CONFIG: allocation_id=%s", step->allocation_id);
    return ENPU_SUCCESS;
}

static int rollback_save_checkpoint(npu_allocator_t *alloc, rollback_step_t *step)
{
    if (!allocation_id_is_valid(step->allocation_id)) {
        ERR("Rollback SAVE_CHECKPOINT skipped: invalid allocation_id, phy=%d vnpu=%d", step->phy_id, step->vnpu_id);
        return ENPU_INVALID_PARAM;
    }

    int ret = allocation_registry_release_by_id(alloc->registry, step->allocation_id);
    if (ret != ENPU_SUCCESS) {
        ERR("Rollback SAVE_CHECKPOINT failed: allocation_id=%s ret=%d", step->allocation_id, ret);
        return ret;
    }

    LOG("Rollback SAVE_CHECKPOINT: allocation_id=%s", step->allocation_id);
    return ENPU_SUCCESS;
}

int rollback_log_rollback(rollback_log_t *log, npu_allocator_t *alloc)
{
    if (!log) {
        ERR("NULL log");
        return ENPU_INVALID_PARAM;
    }

    if (!alloc) {
        ERR("NULL allocator");
        return ENPU_INVALID_PARAM;
    }

    int total = log->step_count;
    int failed = 0;
    LOG("Rollback %d steps", total);

    for (int i = total - 1; i >= 0; i--) {
        rollback_step_t *step = &log->steps[i];
        npu_node_t *node = npu_tree_get_leaf(alloc->tree, step->phy_id);
        int ret;

        LOG("Rollback step %d: type=%d, phy=%d, vnpu=%d", i, step->type, step->phy_id, step->vnpu_id);

        switch (step->type) {
            case STEP_MARK_OCCUPIED:
                ret = rollback_mark_occupied(alloc, step, node);
                break;
            case STEP_UPDATE_QUOTA:
                ret = rollback_update_quota(alloc, step, node);
                break;
            case STEP_ATTACH_POLICY:
                ret = rollback_attach_policy(alloc, step, node);
                break;
            case STEP_CREATE_SHM:
                ret = rollback_create_shm(alloc, step);
                break;
            case STEP_WRITE_CONFIG:
                ret = rollback_write_config(alloc, step);
                break;
            case STEP_SAVE_CHECKPOINT:
                ret = rollback_save_checkpoint(alloc, step);
                break;
            default:
                ERR("Unknown step type: %d", step->type);
                ret = ENPU_FAIL;
                break;
        }

        if (ret != ENPU_SUCCESS) {
            failed++;
        }
    }

    log->step_count = 0;
    if (failed > 0) {
        ERR("Rollback finished: %d/%d step(s) failed, resources may leak", failed, total);
        return ENPU_FAIL;
    }
    LOG("Rollback complete");
    return ENPU_SUCCESS;
}
