/*
 * Copyright (c) Huawei Technologies Co., Ltd. 2025-2026. All rights reserved.
 * enpu-manager is licensed under Mulan PSL v2.
 */

#include <securec.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "common.h"
#include "dcmi_adapter.h"
#include "dcmi_stub_adapter.h"
#include "dcmi_wrapper.h"
#include "securec.h"

typedef struct dcmi_stub_context {
    dcmi_adapter_t adapter;
    dcmi_stub_config_t config;
} dcmi_stub_context_t;

static int stub_get_device_count(dcmi_adapter_t *adapter, int *count)
{
    if (adapter == NULL || count == NULL) {
        return ENPU_INVALID_PARAM;
    }

    dcmi_stub_context_t *ctx = (dcmi_stub_context_t *)adapter;
    *count = ctx->config.device_count;
    return ENPU_SUCCESS;
}

static int stub_get_device_list(dcmi_adapter_t *adapter, int *logic_ids, int max_count, int *actual_count)
{
    if (adapter == NULL || logic_ids == NULL || actual_count == NULL) {
        return ENPU_INVALID_PARAM;
    }

    dcmi_stub_context_t *ctx = (dcmi_stub_context_t *)adapter;
    int count = ctx->config.device_count;

    if (max_count < count) {
        count = max_count;
    }

    for (int i = 0; i < count; i++) {
        logic_ids[i] = i;
    }
    *actual_count = count;

    return ENPU_SUCCESS;
}

static int stub_get_device_info(dcmi_adapter_t *adapter, int logic_id, dcmi_device_info_t *info)
{
    if (adapter == NULL || info == NULL) {
        return ENPU_INVALID_PARAM;
    }

    dcmi_stub_context_t *ctx = (dcmi_stub_context_t *)adapter;

    if (logic_id < 0 || logic_id >= ctx->config.device_count) {
        return ENPU_INVALID_PARAM;
    }

    (void)memset_s(info, sizeof(dcmi_device_info_t), 0, sizeof(dcmi_device_info_t));
    info->logic_id = logic_id;
    info->card_id = logic_id / 4;
    info->device_id = logic_id % 4;
    info->total_memory = ctx->config.total_memory_mb;
    info->total_aicore = ctx->config.total_aicore;
    int ret = snprintf_s(info->uuid, MAX_UUID_LEN, MAX_UUID_LEN - 1, "stub-npu-%d-%04x", logic_id, logic_id);
    if (ret < 0) {
        return ENPU_FAIL;
    }

    return ENPU_SUCCESS;
}

static int stub_get_die_info(dcmi_adapter_t *adapter, int logic_id, dcmi_die_info_t *die_info)
{
    if (adapter == NULL || die_info == NULL) {
        return ENPU_INVALID_PARAM;
    }

    dcmi_stub_context_t *ctx = (dcmi_stub_context_t *)adapter;

    if (logic_id < 0 || logic_id >= ctx->config.device_count) {
        return ENPU_INVALID_PARAM;
    }

    (void)memset_s(die_info, sizeof(dcmi_die_info_t), 0, sizeof(dcmi_die_info_t));
    die_info->phy_id = logic_id;
    die_info->card_id = logic_id / 4;
    die_info->device_id = logic_id % 4;
    int ret = snprintf_s(die_info->die_id, DIE_ID_LEN, DIE_ID_LEN - 1, "14422CC3-2040D918-27A73226-80B40A0A-BB1000%02d",
                         logic_id);
    if (ret < 0) {
        return ENPU_FAIL;
    }
    ret = snprintf_s(die_info->shm_id, DIE_ID_LEN, DIE_ID_LEN - 1, "%s", die_info->die_id);
    if (ret < 0) {
        return ENPU_FAIL;
    }

    return ENPU_SUCCESS;
}

static int stub_get_memory_info(dcmi_adapter_t *adapter, int logic_id, dcmi_memory_info_t *mem_info)
{
    if (adapter == NULL || mem_info == NULL) {
        return ENPU_INVALID_PARAM;
    }

    dcmi_stub_context_t *ctx = (dcmi_stub_context_t *)adapter;

    if (logic_id < 0 || logic_id >= ctx->config.device_count) {
        return ENPU_INVALID_PARAM;
    }

    uint64_t total_bytes = (uint64_t)ctx->config.total_memory_mb * 1024ULL * 1024ULL;
    uint64_t used_bytes = 1024ULL * 1024ULL;

    (void)memset_s(mem_info, sizeof(dcmi_memory_info_t), 0, sizeof(dcmi_memory_info_t));
    mem_info->total_memory = total_bytes;
    mem_info->used_memory = used_bytes;
    mem_info->free_memory = total_bytes - used_bytes;

    return ENPU_SUCCESS;
}

static int stub_get_chip_info(dcmi_adapter_t *adapter, int logic_id, uint32_t *aicore_num, uint32_t *hbm_size)
{
    if (adapter == NULL || aicore_num == NULL || hbm_size == NULL) {
        return ENPU_INVALID_PARAM;
    }

    dcmi_stub_context_t *ctx = (dcmi_stub_context_t *)adapter;

    if (logic_id < 0 || logic_id >= ctx->config.device_count) {
        return ENPU_INVALID_PARAM;
    }

    *aicore_num = ctx->config.total_aicore;
    *hbm_size = ctx->config.total_memory_mb;

    return ENPU_SUCCESS;
}

dcmi_adapter_t *dcmi_adapter_create_stub(const dcmi_stub_config_t *config)
{
    if (config == NULL) {
        return NULL;
    }

    dcmi_stub_context_t *ctx = (dcmi_stub_context_t *)malloc(sizeof(dcmi_stub_context_t));
    if (ctx == NULL) {
        return NULL;
    }

    (void)memset_s(ctx, sizeof(dcmi_stub_context_t), 0, sizeof(dcmi_stub_context_t));
    ctx->config = *config;

    ctx->adapter.get_device_count = stub_get_device_count;
    ctx->adapter.get_device_list = stub_get_device_list;
    ctx->adapter.get_device_info = stub_get_device_info;
    ctx->adapter.get_die_info = stub_get_die_info;
    ctx->adapter.get_memory_info = stub_get_memory_info;
    ctx->adapter.get_chip_info = stub_get_chip_info;

    return &ctx->adapter;
}

void dcmi_adapter_destroy_stub(dcmi_adapter_t *adapter)
{
    if (adapter == NULL) {
        return;
    }

    dcmi_stub_context_t *ctx = (dcmi_stub_context_t *)adapter;
    free(ctx);
}
