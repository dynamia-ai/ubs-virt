/*
 * Copyright (c) Huawei Technologies Co., Ltd. 2026. All rights reserved.
 * enpu-manager is licensed under Mulan PSL v2.
 */
#include "config_manager.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include "../internal/npu_allocator.h"
#include "securec.h"
#include "storage_adapter.h"
static int fail_save(storage_adapter_t *adapter, allocation_t **allocs, int count)
{
    (void)adapter;
    (void)allocs;
    (void)count;
    return ENPU_FAIL;
}
int main(void)
{
    char base_path[] = "/tmp/enpu-config-test-XXXXXX";
    assert(mkdtemp(base_path) != NULL);
    config_manager_t *mgr = config_manager_create(base_path);
    assert(mgr != NULL);
    const char *allocation_id = "5d861bbe-0c3e-4b10-84ab-4c60c53ad2d4";
    vnpu_config_t config = {
        .phy_npu_id = 0,
        .vnpu_id = 1,
        .aicore_quota = 40,
        .memory_request = 1024,
        .memory_limit = 2048,
        .scheduling_policy = 1,
    };
    assert(strcpy_s(config.allocation_id, sizeof(config.allocation_id), allocation_id) == 0);
    assert(strcpy_s(config.shm_id, sizeof(config.shm_id), "stats-domain") == 0);
    assert(config_manager_write_config(mgr, allocation_id, &config) == ENPU_SUCCESS);
    char config_path[MAX_PATH_LEN] = {0};
    assert(config_manager_get_config_path(mgr, allocation_id, config_path, sizeof(config_path)) != NULL);
    assert(strstr(config_path, "/allocations/5d861bbe-0c3e-4b10-84ab-4c60c53ad2d4/npu_info.config") != NULL);
    FILE *file = fopen(config_path, "r");
    assert(file != NULL);
    char content[1024] = {0};
    assert(fread(content, 1, sizeof(content) - 1, file) > 0);
    assert(fclose(file) == 0);
    assert(strstr(content, "allocation-id=5d861bbe-0c3e-4b10-84ab-4c60c53ad2d4\n") != NULL);
    storage_adapter_t *adapter = memory_storage_adapter_create();
    assert(adapter != NULL);
    allocation_registry_t *registry = allocation_registry_create_with_adapter(adapter);
    assert(registry != NULL);
    allocation_t allocation = {0}, found = {0};
    assert(strcpy_s(allocation.allocation_id, sizeof(allocation.allocation_id), allocation_id) == 0);
    assert(allocation_registry_register(registry, &allocation) == ENPU_SUCCESS);
    npu_allocator_t allocator = {.lock = PTHREAD_MUTEX_INITIALIZER, .registry = registry, .config_mgr = mgr};
    storage_save_func save = adapter->save;
    adapter->save = fail_save;
    assert(npu_allocator_release_by_id(&allocator, allocation_id) == ENPU_FAIL);
    assert(access(config_path, F_OK) == 0); /* Persistence failure must precede irreversible cleanup. */
    assert(allocation_registry_get_by_id(registry, allocation_id, &found) == ENPU_SUCCESS);
    adapter->save = save;
    assert(npu_allocator_release_by_id(&allocator, allocation_id) == ENPU_SUCCESS);
    assert(allocation_registry_get_by_id(registry, allocation_id, &found) == ENPU_NOT_FOUND);
    assert(npu_allocator_release_by_id(&allocator, allocation_id) == ENPU_NOT_FOUND);
    assert(pthread_mutex_destroy(&allocator.lock) == 0);
    assert(allocation_registry_destroy(registry) == ENPU_SUCCESS);
    assert(storage_adapter_destroy(adapter) == ENPU_SUCCESS);
    assert(access(config_path, F_OK) != 0);
    assert(config_manager_destroy(mgr) == ENPU_SUCCESS);
    char allocations_path[MAX_PATH_LEN] = {0};
    assert(snprintf_s(allocations_path, sizeof(allocations_path), sizeof(allocations_path) - 1, "%s/allocations",
                      base_path) >= 0);
    assert(rmdir(allocations_path) == 0);
    assert(rmdir(base_path) == 0);
    return 0;
}
