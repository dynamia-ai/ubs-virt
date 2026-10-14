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

#include "hash_map.h"
#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include "log.h"

static size_t hash_ptr(void *ptr, size_t capacity)
{
    if (capacity == 0) {
        return 0;
    }
    uintptr_t addr = (uintptr_t)ptr;
    addr = addr * MOD;
    return addr % capacity;
}

HashMap *hashmap_create(size_t capacity)
{
    if (capacity == 0) {
        LOG_ERROR("Hashmap capacity must be greater than 0.");
        return NULL;
    }
    if (capacity > SIZE_MAX / sizeof(HashNode *)) {
        LOG_ERROR("Hashmap capacity is too large.");
        return NULL;
    }
    HashMap *map = (HashMap *)malloc(sizeof(HashMap));
    if (!map) {
        LOG_ERROR("Map malloc failed when hashmap was created.");
        return NULL;
    }

    map->capacity = capacity;
    map->size = 0;
    map->buckets = (HashNode **)calloc(capacity, sizeof(HashNode *));

    if (!map->buckets) {
        free(map);
        LOG_ERROR("Buckets calloc failed when hashmap was created.");
        return NULL;
    }

    return map;
}

int hashmap_put(HashMap *map, void *key, void *ptr, bool capture_status)
{
    if (!map || !key) {
        LOG_ERROR("Hashmap put map or key is null.");
        return -1;
    }

    size_t idx = hash_ptr(key, map->capacity);
    HashNode *node = map->buckets[idx];

    // If the key exists, update the value
    while (node) {
        if (node->key == key) {
            node->value.ptr = ptr;
            node->value.capture_status = capture_status;
            return 0;
        }
        node = node->next;
    }

    HashNode *new_node = (HashNode *)malloc(sizeof(HashNode));
    if (!new_node) {
        LOG_ERROR("Hashmap put malloc node failed.");
        return -1;
    }

    new_node->key = key;
    new_node->value.ptr = ptr;
    new_node->value.capture_status = capture_status;
    new_node->next = map->buckets[idx];
    map->buckets[idx] = new_node;
    map->size++;

    return 0;
}

int hashmap_get(HashMap *map, void *key, MapValue *value)
{
    if (!map || !key || !value) {
        LOG_ERROR("Param of hashmap get is null.");
        return -1;
    }

    size_t idx = hash_ptr(key, map->capacity);
    HashNode *node = map->buckets[idx];

    while (node) {
        if (node->key == key) {
            *value = node->value;
            return 0;
        }
        node = node->next;
    }
    LOG_ERROR("Hashmap get failed.");
    return -1;
}

int hashmap_get_ptr(HashMap *map, void *key, void **ptr)
{
    if (!map || !key || !ptr) {
        LOG_ERROR("Param of hashmap get ptr is null.");
        return -1;
    }

    size_t idx = hash_ptr(key, map->capacity);
    HashNode *node = map->buckets[idx];

    while (node) {
        if (node->key == key) {
            *ptr = node->value.ptr;
            return 0;
        }
        node = node->next;
    }
    LOG_DEBUG("Hashmap get ptr failed.");
    return -1;
}

int hashmap_get_capture_status(HashMap *map, void *key, bool *capture_status)
{
    if (!map || !key || !capture_status) {
        LOG_ERROR("Param of hashmap get capture status is null.");
        return -1;
    }

    size_t idx = hash_ptr(key, map->capacity);
    HashNode *node = map->buckets[idx];

    while (node) {
        if (node->key == key) {
            *capture_status = node->value.capture_status;
            return 0;
        }
        node = node->next;
    }
    LOG_DEBUG("Hashmap get capture status failed.");
    return -1;
}

int hashmap_remove(HashMap *map, void *key)
{
    if (!map || !key) {
        LOG_ERROR("Param of hashmap remove is null.");
        return -1;
    }

    size_t idx = hash_ptr(key, map->capacity);
    HashNode *node = map->buckets[idx];
    HashNode *prev = NULL;

    while (node) {
        if (node->key == key) {
            if (prev) {
                prev->next = node->next;
            } else {
                map->buckets[idx] = node->next;
            }
            free(node);
            map->size--;
            return 0;
        }
        prev = node;
        node = node->next;
    }
    LOG_ERROR("Hashmap remove failed.");
    return -1;
}

int hashmap_contains(HashMap *map, void *key)
{
    if (!map || !key) {
        LOG_ERROR("Param of hashmap contains is null.");
        return 0;
    }

    size_t idx = hash_ptr(key, map->capacity);
    HashNode *node = map->buckets[idx];

    while (node) {
        if (node->key == key) {
            return 1;
        }
        node = node->next;
    }
    return 0;
}

size_t hashmap_size(HashMap *map)
{
    return map ? map->size : 0;
}

void hashmap_destroy(HashMap *map)
{
    if (!map) {
        LOG_ERROR("Param of hashmap destroy is null.");
        return;
    }

    for (size_t i = 0; i < map->capacity; i++) {
        HashNode *node = map->buckets[i];
        while (node) {
            HashNode *temp = node;
            node = node->next;
            free(temp);
        }
    }

    free(map->buckets);
    free(map);
}