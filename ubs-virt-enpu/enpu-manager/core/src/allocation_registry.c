/*
 * Copyright (c) Huawei Technologies Co., Ltd. 2025-2026. All rights reserved.
 * enpu-manager is licensed under Mulan PSL v2.
 */

#include <pthread.h>
#include <stdlib.h>
#include <string.h>
#include "allocation.h"
#include "log.h"
#include "securec.h"
#include "storage_adapter.h"

#define ALLOC_REGISTRY_INIT_CAPACITY 64

struct allocation_registry {
    allocation_t *entries;
    int count;
    int capacity;
    pthread_mutex_t lock;
    storage_adapter_t *adapter;
};

static int persist_to_adapter(allocation_registry_t *registry);

allocation_registry_t *allocation_registry_create(void)
{
    return allocation_registry_create_with_adapter(NULL);
}

allocation_registry_t *allocation_registry_create_with_adapter(storage_adapter_t *adapter)
{
    allocation_registry_t *registry = calloc(1, sizeof(allocation_registry_t));
    if (!registry) {
        return NULL;
    }

    registry->entries = calloc(ALLOC_REGISTRY_INIT_CAPACITY, sizeof(allocation_t));
    if (!registry->entries) {
        free(registry);
        return NULL;
    }

    registry->capacity = ALLOC_REGISTRY_INIT_CAPACITY;
    registry->count = 0;
    registry->adapter = adapter;
    pthread_mutex_init(&registry->lock, NULL);

    return registry;
}

int allocation_registry_destroy(allocation_registry_t *registry)
{
    if (!registry) {
        return ENPU_INVALID_PARAM;
    }

    pthread_mutex_destroy(&registry->lock);
    free(registry->entries);
    free(registry);

    return ENPU_SUCCESS;
}

static int find_entry_index_by_id(allocation_registry_t *registry, const char *allocation_id)
{
    for (int i = 0; i < registry->count; i++) {
        if (strcmp(registry->entries[i].allocation_id, allocation_id) == 0) {
            return i;
        }
    }
    return -1;
}

/* The caller must hold registry->lock. The index is valid only on success. */
static int find_unique_entry_index_by_labels(allocation_registry_t *registry, const char *pod_uid,
                                             const char *container_name, int *index)
{
    int first = -1;
    for (int i = 0; i < registry->count; i++) {
        if (strcmp(registry->entries[i].pod_uid, pod_uid) == 0 &&
            strcmp(registry->entries[i].container_name, container_name) == 0) {
            if (first >= 0) {
                return ENPU_ALREADY_EXISTS;
            }
            first = i;
        }
    }
    *index = first;
    return first < 0 ? ENPU_NOT_FOUND : ENPU_SUCCESS;
}

static int remove_entry(allocation_registry_t *registry, int index)
{
    allocation_t removed = registry->entries[index];
    if (index < registry->count - 1) {
        size_t move_bytes = (size_t)(registry->count - index - 1) * sizeof(allocation_t);
        if (memmove_s(&registry->entries[index], move_bytes, &registry->entries[index + 1], move_bytes) != 0) {
            return ENPU_FAIL;
        }
    }
    registry->count--;
    int ret = persist_to_adapter(registry);
    if (ret == ENPU_SUCCESS) {
        return ENPU_SUCCESS;
    }

    if (index < registry->count) {
        size_t move_bytes = (size_t)(registry->count - index) * sizeof(allocation_t);
        size_t dest_bytes = (size_t)(registry->capacity - index - 1) * sizeof(allocation_t);
        if (memmove_s(&registry->entries[index + 1], dest_bytes, &registry->entries[index], move_bytes) != 0) {
            LOG_ERROR("Failed to restore allocation registry after persistence failure");
            return ret;
        }
    }
    registry->entries[index] = removed;
    registry->count++;
    return ret;
}

static int persist_to_adapter(allocation_registry_t *registry)
{
    if (!registry->adapter) {
        return ENPU_SUCCESS;
    }

    allocation_t *allocs[MAX_ALLOC_COUNT] = {0};
    for (int i = 0; i < registry->count; i++) {
        allocs[i] = &registry->entries[i];
    }

    return storage_adapter_save(registry->adapter, allocs, registry->count);
}

/* The caller must hold registry->lock. */
static int update_shm_id_locked(allocation_registry_t *registry, int index, const char *new_shm_id)
{
    if (strnlen(new_shm_id, DIE_ID_LEN) >= DIE_ID_LEN) {
        return ENPU_INVALID_PARAM;
    }

    allocation_t original = registry->entries[index];
    if (strcpy_s(registry->entries[index].shm_id, sizeof(registry->entries[index].shm_id), new_shm_id) != 0) {
        registry->entries[index] = original;
        return ENPU_FAIL;
    }

    int ret = persist_to_adapter(registry);
    if (ret != ENPU_SUCCESS) {
        registry->entries[index] = original;
        LOG_ERROR("Failed to persist shm_id update: allocation_id=%s ret=%d", registry->entries[index].allocation_id,
                  ret);
    }
    return ret;
}

int allocation_registry_register(allocation_registry_t *registry, const allocation_t *allocation)
{
    if (!registry || !allocation) {
        return ENPU_INVALID_PARAM;
    }
    if (!allocation_id_is_valid(allocation->allocation_id)) {
        return ENPU_INVALID_PARAM;
    }

    pthread_mutex_lock(&registry->lock);

    int idx = find_entry_index_by_id(registry, allocation->allocation_id);
    if (idx >= 0) {
        pthread_mutex_unlock(&registry->lock);
        return ENPU_ALREADY_EXISTS;
    }

    if (registry->count >= MAX_ALLOC_COUNT) {
        pthread_mutex_unlock(&registry->lock);
        return ENPU_NO_RESOURCE;
    }

    if (registry->count >= registry->capacity) {
        int new_capacity = registry->capacity * 2;
        if (new_capacity > MAX_ALLOC_COUNT) {
            new_capacity = MAX_ALLOC_COUNT;
        }
        allocation_t *new_entries = (allocation_t *)malloc((size_t)new_capacity * sizeof(allocation_t));
        if (!new_entries) {
            pthread_mutex_unlock(&registry->lock);
            return ENPU_NO_RESOURCE;
        }
        memcpy_s(new_entries, (size_t)new_capacity * sizeof(allocation_t), registry->entries,
                 (size_t)registry->count * sizeof(allocation_t));
        free(registry->entries);
        registry->entries = new_entries;
        registry->capacity = new_capacity;
    }

    memcpy_s(&registry->entries[registry->count], sizeof(allocation_t), allocation, sizeof(allocation_t));
    registry->count++;

    int ret = persist_to_adapter(registry);
    if (ret != ENPU_SUCCESS) {
        registry->count--;
        (void)memset_s(&registry->entries[registry->count], sizeof(allocation_t), 0, sizeof(allocation_t));
    }

    pthread_mutex_unlock(&registry->lock);
    return ret;
}

int allocation_registry_release_by_id(allocation_registry_t *registry, const char *allocation_id)
{
    if (!registry || !allocation_id_is_valid(allocation_id)) {
        return ENPU_INVALID_PARAM;
    }
    pthread_mutex_lock(&registry->lock);
    int idx = find_entry_index_by_id(registry, allocation_id);
    int ret = idx < 0 ? ENPU_NOT_FOUND : remove_entry(registry, idx);
    pthread_mutex_unlock(&registry->lock);
    return ret;
}

int allocation_registry_update_shm_id_by_id(allocation_registry_t *registry, const char *allocation_id,
                                            const char *new_shm_id)
{
    if (!registry || !allocation_id_is_valid(allocation_id) || !new_shm_id) {
        return ENPU_INVALID_PARAM;
    }
    pthread_mutex_lock(&registry->lock);
    int idx = find_entry_index_by_id(registry, allocation_id);
    int ret = idx < 0 ? ENPU_NOT_FOUND : update_shm_id_locked(registry, idx, new_shm_id);
    pthread_mutex_unlock(&registry->lock);
    return ret;
}

int allocation_registry_get(allocation_registry_t *registry, const char *pod_uid, const char *container_name,
                            allocation_t *allocation)
{
    if (!registry || !pod_uid || !container_name || !allocation) {
        return ENPU_INVALID_PARAM;
    }

    pthread_mutex_lock(&registry->lock);

    int idx;
    int ret = find_unique_entry_index_by_labels(registry, pod_uid, container_name, &idx);
    if (ret != ENPU_SUCCESS) {
        pthread_mutex_unlock(&registry->lock);
        return ret;
    }

    memcpy_s(allocation, sizeof(allocation_t), &registry->entries[idx], sizeof(allocation_t));

    pthread_mutex_unlock(&registry->lock);
    return ENPU_SUCCESS;
}

int allocation_registry_get_by_id(allocation_registry_t *registry, const char *allocation_id, allocation_t *allocation)
{
    if (!registry || !allocation_id_is_valid(allocation_id) || !allocation) {
        return ENPU_INVALID_PARAM;
    }
    pthread_mutex_lock(&registry->lock);
    int idx = find_entry_index_by_id(registry, allocation_id);
    if (idx >= 0) {
        *allocation = registry->entries[idx];
    }
    pthread_mutex_unlock(&registry->lock);
    return idx < 0 ? ENPU_NOT_FOUND : ENPU_SUCCESS;
}

int allocation_registry_list(allocation_registry_t *registry, allocation_t *allocations, int *count)
{
    if (!registry || !allocations || !count) {
        return ENPU_INVALID_PARAM;
    }

    pthread_mutex_lock(&registry->lock);

    *count = registry->count;
    if (registry->count > 0) {
        memcpy_s(allocations, (size_t)registry->count * sizeof(allocation_t), registry->entries,
                 (size_t)registry->count * sizeof(allocation_t));
    }

    pthread_mutex_unlock(&registry->lock);
    return ENPU_SUCCESS;
}

int allocation_registry_get_by_phy_id_vnpu_id(allocation_registry_t *registry, int32_t phy_id, int32_t vnpu_id,
                                              allocation_t *allocation)
{
    if (!registry || !allocation) {
        return ENPU_INVALID_PARAM;
    }

    pthread_mutex_lock(&registry->lock);

    for (int i = 0; i < registry->count; i++) {
        if (registry->entries[i].phy_id == phy_id && registry->entries[i].vnpu_id == vnpu_id) {
            memcpy_s(allocation, sizeof(allocation_t), &registry->entries[i], sizeof(allocation_t));
            pthread_mutex_unlock(&registry->lock);
            return ENPU_SUCCESS;
        }
    }

    pthread_mutex_unlock(&registry->lock);
    return ENPU_NOT_FOUND;
}

int allocation_registry_list_by_phy_id(allocation_registry_t *registry, int32_t phy_id, allocation_t *allocations,
                                       int *count)
{
    if (!registry || !allocations || !count) {
        return ENPU_INVALID_PARAM;
    }

    pthread_mutex_lock(&registry->lock);

    int found = 0;
    for (int i = 0; i < registry->count; i++) {
        if (registry->entries[i].phy_id == phy_id) {
            memcpy_s(&allocations[found], sizeof(allocation_t), &registry->entries[i], sizeof(allocation_t));
            found++;
        }
    }

    *count = found;
    pthread_mutex_unlock(&registry->lock);
    return ENPU_SUCCESS;
}

int allocation_registry_save(allocation_registry_t *registry)
{
    if (!registry) {
        return ENPU_INVALID_PARAM;
    }

    pthread_mutex_lock(&registry->lock);
    int ret = persist_to_adapter(registry);
    pthread_mutex_unlock(&registry->lock);

    return ret;
}

int allocation_registry_recover(allocation_registry_t *registry)
{
    if (!registry || !registry->adapter) {
        return ENPU_INVALID_PARAM;
    }

    pthread_mutex_lock(&registry->lock);

    allocation_t loaded[MAX_ALLOC_COUNT] = {0};
    int count = 0;
    int ret = storage_adapter_load(registry->adapter, loaded, &count);
    if (ret != ENPU_SUCCESS) {
        pthread_mutex_unlock(&registry->lock);
        return ret;
    }
    if (count < 0 || count > MAX_ALLOC_COUNT) {
        LOG_ERROR("Invalid allocation count in checkpoint: count=%d", count);
        pthread_mutex_unlock(&registry->lock);
        return ENPU_FAIL;
    }

    for (int i = 0; i < count; ++i) {
        if (!allocation_id_is_valid(loaded[i].allocation_id)) {
            LOG_ERROR("Invalid allocation_id in checkpoint: index=%d value=%s", i, loaded[i].allocation_id);
            pthread_mutex_unlock(&registry->lock);
            return ENPU_FAIL;
        }
        for (int j = 0; j < i; ++j) {
            if (strcmp(loaded[i].allocation_id, loaded[j].allocation_id) == 0) {
                LOG_ERROR("Duplicate allocation_id in checkpoint: index=%d previous=%d value=%s", i, j,
                          loaded[i].allocation_id);
                pthread_mutex_unlock(&registry->lock);
                return ENPU_FAIL;
            }
        }
    }

    if (count > registry->capacity) {
        int new_capacity = count;
        allocation_t *new_entries = (allocation_t *)malloc((size_t)new_capacity * sizeof(allocation_t));
        if (!new_entries) {
            pthread_mutex_unlock(&registry->lock);
            return ENPU_NO_RESOURCE;
        }
        memcpy_s(new_entries, (size_t)new_capacity * sizeof(allocation_t), registry->entries,
                 (size_t)registry->count * sizeof(allocation_t));
        free(registry->entries);
        registry->entries = new_entries;
        registry->capacity = new_capacity;
    }

    if (count > 0 && memcpy_s(registry->entries, (size_t)registry->capacity * sizeof(allocation_t), loaded,
                              (size_t)count * sizeof(allocation_t)) != 0) {
        pthread_mutex_unlock(&registry->lock);
        return ENPU_FAIL;
    }
    registry->count = count;

    pthread_mutex_unlock(&registry->lock);
    return ENPU_SUCCESS;
}
