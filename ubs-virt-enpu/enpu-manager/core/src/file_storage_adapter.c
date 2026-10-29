/*
 * Copyright (c) Huawei Technologies Co., Ltd. 2025-2026. All rights reserved.
 * enpu-manager is licensed under Mulan PSL v2.
 */

#include <cjson/cJSON.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include "common.h"
#include "securec.h"
#include "storage_adapter.h"

#define FILE_ADAPTER_DIR_MODE 0755

typedef struct file_adapter_data {
    char path[MAX_PATH_LEN];
} file_adapter_data_t;

// /var/lib/enpu-manager/checkpoint.json → 创建 /var/lib/enpu-manager
static int ensure_parent_dir(const char *file_path)
{
    if (file_path == NULL) {
        return ENPU_INVALID_PARAM;
    }

    char tmp[MAX_PATH_LEN];
    if (snprintf_s(tmp, sizeof(tmp), sizeof(tmp) - 1, "%s", file_path) < 0) {
        return ENPU_FAIL;
    }

    char *slash = strrchr(tmp, '/');
    if (slash == NULL || slash == tmp) {
        // 无目录前缀，或父目录就是根，无需创建
        return ENPU_SUCCESS;
    }
    *slash = '\0'; // tmp 现在是父目录

    for (char *p = tmp + 1; *p; p++) {
        if (*p == '/') {
            *p = '\0';
            if (mkdir(tmp, FILE_ADAPTER_DIR_MODE) != 0 && errno != EEXIST) {
                return ENPU_FAIL;
            }
            *p = '/';
        }
    }
    if (mkdir(tmp, FILE_ADAPTER_DIR_MODE) != 0 && errno != EEXIST) {
        return ENPU_FAIL;
    }
    return ENPU_SUCCESS;
}

static int file_adapter_save(storage_adapter_t *adapter, allocation_t **allocs, int count)
{
    file_adapter_data_t *data = (file_adapter_data_t *)adapter->private_data;
    if (!data || !data->path[0]) {
        return ENPU_FAIL;
    }

    cJSON *root = cJSON_CreateArray();
    if (!root) {
        return ENPU_NO_RESOURCE;
    }

    for (int i = 0; i < count; i++) {
        allocation_t *alloc = allocs[i];
        cJSON *item = cJSON_CreateObject();
        if (!item) {
            cJSON_Delete(root);
            return ENPU_NO_RESOURCE;
        }

        cJSON_AddStringToObject(item, "pod_uid", alloc->pod_uid);
        cJSON_AddStringToObject(item, "container_name", alloc->container_name);
        cJSON_AddNumberToObject(item, "phy_id", alloc->phy_id);
        cJSON_AddNumberToObject(item, "vnpu_id", alloc->vnpu_id);
        cJSON_AddStringToObject(item, "die_id", alloc->die_id);
        cJSON_AddStringToObject(item, "shm_id", alloc->shm_id);
        cJSON_AddNumberToObject(item, "aicore_quota", alloc->aicore_quota);
        cJSON_AddNumberToObject(item, "hbm_quota", alloc->hbm_quota);
        cJSON_AddNumberToObject(item, "hbm_limit", alloc->hbm_limit);
        cJSON_AddNumberToObject(item, "sched_policy", alloc->sched_policy);
        cJSON_AddNumberToObject(item, "swap_priority", alloc->swap_priority);
        cJSON_AddNumberToObject(item, "vnpu_priority", alloc->vnpu_priority);
        cJSON_AddNumberToObject(item, "create_time_ns", alloc->create_time_ns);
        cJSON_AddNumberToObject(item, "update_time_ns", alloc->update_time_ns);
        cJSON_AddNumberToObject(item, "swap_offset", alloc->swap_offset);
        cJSON_AddNumberToObject(item, "swap_size", alloc->swap_size);
        cJSON_AddBoolToObject(item, "swapped", alloc->swapped);

        cJSON_AddItemToArray(root, item);
    }

    char *json_str = cJSON_Print(root);
    cJSON_Delete(root);

    if (!json_str) {
        return ENPU_FAIL;
    }

    if (ensure_parent_dir(data->path) != ENPU_SUCCESS) {
        free(json_str);
        return ENPU_FAIL;
    }

    FILE *fp = fopen(data->path, "w");
    if (!fp) {
        free(json_str);
        return ENPU_FAIL;
    }

    if (fprintf(fp, "%s\n", json_str) < 0) {
        fclose(fp);
        free(json_str);
        return ENPU_FAIL;
    }
    if (fclose(fp) != 0) {
        free(json_str);
        return ENPU_FAIL;
    }
    free(json_str);

    return ENPU_SUCCESS;
}

static int file_adapter_load(storage_adapter_t *adapter, allocation_t *allocs, int *count)
{
    file_adapter_data_t *data = (file_adapter_data_t *)adapter->private_data;
    if (!data || !data->path[0]) {
        return ENPU_FAIL;
    }

    FILE *fp = fopen(data->path, "r");
    if (!fp) {
        *count = 0;
        return ENPU_SUCCESS;
    }

    if (fseek(fp, 0, SEEK_END) != 0) {
        fclose(fp);
        *count = 0;
        return ENPU_FAIL;
    }
    long file_size = ftell(fp);
    if (file_size < 0) {
        fclose(fp);
        *count = 0;
        return ENPU_FAIL;
    }
    if (fseek(fp, 0, SEEK_SET) != 0) {
        fclose(fp);
        *count = 0;
        return ENPU_FAIL;
    }

    char *json_str = calloc(1, file_size + 1);
    if (!json_str) {
        fclose(fp);
        return ENPU_NO_RESOURCE;
    }

    if (file_size > 0 && fread(json_str, 1, file_size, fp) != (size_t)file_size) {
        fclose(fp);
        free(json_str);
        return ENPU_FAIL;
    }
    if (fclose(fp) != 0) {
        free(json_str);
        return ENPU_FAIL;
    }

    cJSON *root = cJSON_Parse(json_str);
    free(json_str);

    if (!root) {
        *count = 0;
        return ENPU_SUCCESS;
    }

    cJSON *entries_arr = root;
    if (cJSON_IsObject(root)) {
        entries_arr = cJSON_GetObjectItem(root, "entries");
    }

    if (!entries_arr || !cJSON_IsArray(entries_arr)) {
        cJSON_Delete(root);
        *count = 0;
        return ENPU_SUCCESS;
    }

    int array_size = cJSON_GetArraySize(entries_arr);
    *count = 0;

    for (int i = 0; i < array_size && i < MAX_ALLOC_COUNT; i++) {
        cJSON *item = cJSON_GetArrayItem(entries_arr, i);
        if (!item) {
            continue;
        }

        allocation_t *alloc = &allocs[i];
        if (memset_s(alloc, sizeof(allocation_t), 0, sizeof(allocation_t)) != 0) {
            cJSON_Delete(root);
            return ENPU_FAIL;
        }

        cJSON *pod_uid = cJSON_GetObjectItem(item, "pod_uid");
        cJSON *container_name = cJSON_GetObjectItem(item, "container_name");
        cJSON *phy_id = cJSON_GetObjectItem(item, "phy_id");
        cJSON *vnpu_id = cJSON_GetObjectItem(item, "vnpu_id");
        cJSON *die_id = cJSON_GetObjectItem(item, "die_id");
        cJSON *shm_id = cJSON_GetObjectItem(item, "shm_id");
        cJSON *aicore_quota = cJSON_GetObjectItem(item, "aicore_quota");
        cJSON *hbm_quota = cJSON_GetObjectItem(item, "hbm_quota");
        cJSON *hbm_limit = cJSON_GetObjectItem(item, "hbm_limit");
        cJSON *sched_policy = cJSON_GetObjectItem(item, "sched_policy");
        cJSON *swap_priority = cJSON_GetObjectItem(item, "swap_priority");
        cJSON *vnpu_priority = cJSON_GetObjectItem(item, "vnpu_priority");
        if (vnpu_priority == NULL) {
            vnpu_priority = cJSON_GetObjectItem(item, "priority"); /* 兼容旧版持久化数据 */
        }
        cJSON *create_time_ns = cJSON_GetObjectItem(item, "create_time_ns");
        cJSON *update_time_ns = cJSON_GetObjectItem(item, "update_time_ns");
        cJSON *swap_offset = cJSON_GetObjectItem(item, "swap_offset");
        cJSON *swap_size = cJSON_GetObjectItem(item, "swap_size");
        cJSON *swapped = cJSON_GetObjectItem(item, "swapped");

        if (pod_uid && pod_uid->valuestring) {
            if (strncpy_s(alloc->pod_uid, MAX_UUID_LEN, pod_uid->valuestring, MAX_UUID_LEN - 1) != 0) {
                cJSON_Delete(root);
                return ENPU_FAIL;
            }
        }
        if (container_name && container_name->valuestring) {
            if (strncpy_s(alloc->container_name, MAX_NAME_LEN, container_name->valuestring, MAX_NAME_LEN - 1) != 0) {
                cJSON_Delete(root);
                return ENPU_FAIL;
            }
        }
        if (phy_id) {
            alloc->phy_id = phy_id->valueint;
        }
        if (vnpu_id) {
            alloc->vnpu_id = vnpu_id->valueint;
        }
        if (die_id && die_id->valuestring) {
            if (strncpy_s(alloc->die_id, DIE_ID_LEN, die_id->valuestring, DIE_ID_LEN - 1) != 0) {
                cJSON_Delete(root);
                return ENPU_FAIL;
            }
        }
        if (shm_id && shm_id->valuestring) {
            if (strncpy_s(alloc->shm_id, DIE_ID_LEN, shm_id->valuestring, DIE_ID_LEN - 1) != 0) {
                cJSON_Delete(root);
                return ENPU_FAIL;
            }
        }
        if (aicore_quota) {
            alloc->aicore_quota = aicore_quota->valueint;
        }
        if (hbm_quota) {
            alloc->hbm_quota = (uint64_t)hbm_quota->valuedouble;
        }
        if (hbm_limit) {
            alloc->hbm_limit = (uint64_t)hbm_limit->valuedouble;
        } else if (hbm_quota) {
            alloc->hbm_limit = alloc->hbm_quota;
        }
        if (sched_policy) {
            alloc->sched_policy = sched_policy->valueint;
        }
        if (vnpu_priority &&
            (vnpu_priority->valueint == VNPU_PRIORITY_NORMAL || vnpu_priority->valueint == VNPU_PRIORITY_HIGH)) {
            alloc->vnpu_priority = vnpu_priority->valueint;
        } else {
            alloc->vnpu_priority = VNPU_PRIORITY_NORMAL;
        }
        if (swap_priority && swap_priority->valueint >= SWAP_PRIORITY_HIGH &&
            swap_priority->valueint <= SWAP_PRIORITY_LOW) {
            alloc->swap_priority = swap_priority->valueint;
        } else {
            alloc->swap_priority = SWAP_PRIORITY_MEDIUM;
        }
        if (create_time_ns) {
            alloc->create_time_ns = (uint64_t)create_time_ns->valuedouble;
        }
        if (update_time_ns) {
            alloc->update_time_ns = (uint64_t)update_time_ns->valuedouble;
        }
        if (swap_offset) {
            alloc->swap_offset = (uint64_t)swap_offset->valuedouble;
        }
        if (swap_size) {
            alloc->swap_size = (uint64_t)swap_size->valuedouble;
        }
        if (swapped) {
            alloc->swapped = cJSON_IsTrue(swapped);
        }

        (*count)++;
    }

    cJSON_Delete(root);
    return ENPU_SUCCESS;
}

static int file_adapter_destroy(storage_adapter_t *adapter)
{
    file_adapter_data_t *data = (file_adapter_data_t *)adapter->private_data;
    if (data) {
        free(data);
    }
    free(adapter);
    return ENPU_SUCCESS;
}

storage_adapter_t *file_storage_adapter_create(const char *path)
{
    storage_adapter_t *adapter = calloc(1, sizeof(storage_adapter_t));
    if (!adapter) {
        return NULL;
    }

    file_adapter_data_t *data = calloc(1, sizeof(file_adapter_data_t));
    if (!data) {
        free(adapter);
        return NULL;
    }

    if (path) {
        if (strncpy_s(data->path, MAX_PATH_LEN, path, MAX_PATH_LEN - 1) != 0) {
            free(data);
            free(adapter);
            return NULL;
        }
    }

    adapter->save = file_adapter_save;
    adapter->load = file_adapter_load;
    adapter->destroy = file_adapter_destroy;
    adapter->private_data = data;

    return adapter;
}
