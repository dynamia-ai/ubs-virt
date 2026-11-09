/* Copyright (c) Huawei Technologies Co., Ltd. 2026. All rights reserved.
 * Licensed under Mulan PSL v2. */
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "../internal/npu_allocator.h"
#include "allocation.h"
#include "securec.h"
#include "storage_adapter.h"
static allocation_t make_allocation(const char *pod_uid, const char *container_name)
{
    allocation_t allocation = {0};
    assert(allocation_id_generate(allocation.allocation_id) == ENPU_SUCCESS);
    assert(strcpy_s(allocation.pod_uid, sizeof(allocation.pod_uid), pod_uid) == 0);
    assert(strcpy_s(allocation.container_name, sizeof(allocation.container_name), container_name) == 0);
    return allocation;
}
static int controlled_save(storage_adapter_t *adapter, allocation_t **allocs, int count)
{
    (void)adapter;
    (void)allocs;
    (void)count;
    return ENPU_FAIL;
}
static void test_shm_id_update_rolls_back_on_persistence_failure(void)
{
    storage_adapter_t *adapter = memory_storage_adapter_create();
    assert(adapter != NULL);
    allocation_registry_t *registry = allocation_registry_create_with_adapter(adapter);
    assert(registry != NULL);
    allocation_t allocation = make_allocation("pod", "container");
    assert(strcpy_s(allocation.shm_id, sizeof(allocation.shm_id), "old-domain") == 0);
    assert(allocation_registry_register(registry, &allocation) == ENPU_SUCCESS);
    adapter->save = controlled_save;
    assert(allocation_registry_update_shm_id_by_id(registry, allocation.allocation_id, "new-domain") == ENPU_FAIL);
    allocation_t found = {0};
    assert(allocation_registry_get_by_id(registry, allocation.allocation_id, &found) == ENPU_SUCCESS);
    assert(strcmp(found.shm_id, "old-domain") == 0);
    assert(allocation_registry_destroy(registry) == ENPU_SUCCESS);
    assert(storage_adapter_destroy(adapter) == ENPU_SUCCESS);
}
static void test_uuid_identity_and_labels(void)
{
    allocation_registry_t *registry = allocation_registry_create();
    assert(registry != NULL);
    allocation_t first = make_allocation("", "");
    allocation_t second = make_allocation("", "");
    assert(strcmp(first.allocation_id, second.allocation_id) != 0);
    assert(allocation_registry_register(registry, &first) == ENPU_SUCCESS);
    assert(allocation_registry_register(registry, &second) == ENPU_SUCCESS);
    assert(allocation_registry_register(registry, &first) == ENPU_ALREADY_EXISTS);
    allocation_t found = {0};
    assert(allocation_registry_get_by_id(registry, first.allocation_id, &found) == ENPU_SUCCESS);
    assert(strcmp(found.allocation_id, first.allocation_id) == 0);
    assert(allocation_registry_update_shm_id_by_id(registry, first.allocation_id, "stats-domain") == ENPU_SUCCESS);
    assert(allocation_registry_get_by_id(registry, first.allocation_id, &found) == ENPU_SUCCESS);
    assert(strcmp(found.shm_id, "stats-domain") == 0);
    assert(allocation_registry_get(registry, "", "", &found) == ENPU_ALREADY_EXISTS);
    npu_allocator_t allocator = {.registry = registry};
    assert(npu_allocator_release(&allocator, "", "") == ENPU_ALREADY_EXISTS);
    assert(npu_allocator_release(&allocator, "missing", "missing") == ENPU_NOT_FOUND);
    assert(allocation_registry_release_by_id(registry, first.allocation_id) == ENPU_SUCCESS);
    assert(allocation_registry_get_by_id(registry, first.allocation_id, &found) == ENPU_NOT_FOUND);
    assert(allocation_registry_get(registry, "", "", &found) == ENPU_SUCCESS);
    assert(strcmp(found.allocation_id, second.allocation_id) == 0);
    assert(allocation_registry_destroy(registry) == ENPU_SUCCESS);
}
static void *retry(void *allocator)
{
    alloc_request_t request = {.allocation_id = "550E8400-E29B-41D4-A716-446655440000",
                               .aicore_quota = 50,
                               .hbm_quota = 2048,
                               .hbm_limit = 2048,
                               .sched_policy = 1,
                               .predicate_phy_id = -1};
    alloc_response_t response = {0};
    assert(npu_allocator_allocate(allocator, &request, &response) == ENPU_ALREADY_EXISTS);
    assert(response.result == ENPU_ALREADY_EXISTS);
    assert(strcmp(request.allocation_id, "550e8400-e29b-41d4-a716-446655440000") == 0);
    return NULL;
}
static void test_checkpoint_and_retries(void)
{
    allocation_t invalid = {0};
    allocation_t duplicate = {.allocation_id = "550e8400-e29b-41d4-a716-446655440000", .aicore_quota = 20};
    allocation_t *cases[][2] = {{&invalid, &invalid}, {&duplicate, &duplicate}, {&duplicate}};
    for (int i = 0; i < 3; i++) {
        storage_adapter_t *adapter = memory_storage_adapter_create();
        assert(adapter != NULL);
        assert(storage_adapter_save(adapter, cases[i], i == 2 ? 1 : 2) == ENPU_SUCCESS);
        allocation_registry_t *registry = allocation_registry_create_with_adapter(adapter);
        assert(registry != NULL);
        assert(allocation_registry_recover(registry) == (i == 2 ? ENPU_SUCCESS : ENPU_FAIL));
        if (i == 2) {
            npu_allocator_t allocator = {.lock = PTHREAD_MUTEX_INITIALIZER, .registry = registry};
            pthread_t threads[8];
            for (int j = 0; j < 8; j++) {
                assert(pthread_create(&threads[j], NULL, retry, &allocator) == 0);
            }
            for (int j = 0; j < 8; j++) {
                assert(pthread_join(threads[j], NULL) == 0);
            }
            assert(allocation_registry_get_by_id(registry, duplicate.allocation_id, &duplicate) == ENPU_SUCCESS);
            assert(duplicate.aicore_quota == 20);
            assert(pthread_mutex_destroy(&allocator.lock) == 0);
        }
        assert(allocation_registry_destroy(registry) == ENPU_SUCCESS);
        assert(storage_adapter_destroy(adapter) == ENPU_SUCCESS);
    }
}
int main(void)
{
    assert(!allocation_id_is_valid(""));
    assert(!allocation_id_is_valid("5d861bbe-0c3e-4b10-84ab-4c60c53ad2dX"));
    assert(allocation_id_is_valid("5d861bbe-0c3e-4b10-84ab-4c60c53ad2d4"));
    test_uuid_identity_and_labels();
    test_shm_id_update_rolls_back_on_persistence_failure();
    test_checkpoint_and_retries();
    puts("allocation identity, registry and checkpoint checks passed");
    return 0;
}
