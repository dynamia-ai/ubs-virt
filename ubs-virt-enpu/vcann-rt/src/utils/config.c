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
#include "../include/config.h"
#include <limits.h>

#define TEN_BASE 10
#define MB_TO_B (1024 * 1024)

struct Config config = {0};

void reset_config()
{
    config.phy_npu_id = INVALID_VALUE;
    config.vnpu_id = INVALID_VALUE;
    config.aicore_quota = INVALID_VALUE;
    config.memory_quota = INVALID_VALUE;
    config.memory_request = 0;
    config.memory_limit = 0;
    config.scheduling_policy = INVALID_VALUE;
    config.vnpu_priority = VNPU_PRIORITY_NORMAL;
    (void)memset_s(config.shm_id, sizeof(config.shm_id), 0, sizeof(config.shm_id));
}

int check_int32(int32_t option, const char *option_name)
{
    if (option == INVALID_VALUE) {
        LOG_ERROR("\"%s\" is not set. Please check the config and add it as a new line: \"%s=VALUE\"", option_name,
                  option_name);
        return ENPU_FAIL;
    }
    return ENPU_SUCCESS;
}

int check_str(const char *str, const char *option_name)
{
    if (strlen(str) == 0) {
        LOG_ERROR("\"%s\" is not set. Please check the config and add it as a new line: \"%s=VALUE\"", option_name,
                  option_name);
        return ENPU_FAIL;
    }
    return ENPU_SUCCESS;
}

int check_shm_id(const char *str, const char *option_name)
{
    if (strlen(str) == 0) {
        LOG_ERROR("\"%s\" is not set. Please check the config and add it as a new line: \"%s=VALUE\"", option_name,
                  option_name);
        return ENPU_FAIL;
    }
    if (strchr(str, '/') != NULL) {
        LOG_ERROR("\"%s\" contains invalid character '/'. Shared memory name must not contain path separators.",
                  option_name);
        return ENPU_FAIL;
    }
    if (strstr(str, "..") != NULL) {
        LOG_ERROR("\"%s\" contains invalid sequence '..'. Shared memory name must not contain path traversal.",
                  option_name);
        return ENPU_FAIL;
    }
    return ENPU_SUCCESS;
}

int check_config()
{
    if (config.vnpu_priority != VNPU_PRIORITY_NORMAL && config.vnpu_priority != VNPU_PRIORITY_HIGH) {
        LOG_ERROR("\"%s\" must be 0 (normal) or 1 (high), got %d", OPTION_VNPU_PRIORITY, config.vnpu_priority);
        return 0;
    }
    if (config.vnpu_priority == VNPU_PRIORITY_HIGH) {
        // High priority bypasses compute scheduling; quota and scheduling-policy are unused.
        config.aicore_quota = 0;
        config.scheduling_policy = 3;
    }
    if (config.memory_request == 0 && config.memory_limit == 0) {
        if (config.memory_quota == INVALID_VALUE) {
            LOG_ERROR("Neither \"memory_quota\" nor \"memory_request\"/\"memory_limit\" is configured. Please "
                      "configure \"memory_quota\" alone or both \"memory_request\" and \"memory_limit\".");
            return 0;
        }
        config.memory_request = (uint64_t)config.memory_quota;
        config.memory_limit = (uint64_t)config.memory_quota;
    } else if (config.memory_request == 0 || config.memory_limit == 0) {
        LOG_ERROR("\"memory_request\" and \"memory_limit\" must be configured together, or use \"memory_quota\" "
                  "alone. Please check the config.");
        return 0;
    }

    return check_int32(config.phy_npu_id, OPTION_NPU_ID) == ENPU_SUCCESS &&
           check_int32(config.vnpu_id, OPTION_VNPU_ID) == ENPU_SUCCESS &&
           check_int32(config.aicore_quota, OPTION_AICORE_QUOTA) == ENPU_SUCCESS &&
           check_int32(config.scheduling_policy, OPTION_SCHEDULING_POLICY) == ENPU_SUCCESS &&
           check_shm_id(config.shm_id, OPTION_SHM_ID) == ENPU_SUCCESS;
}

int load_int32(const char *key, const char *value, int32_t *ret_value)
{
    CHECK_COND_RETURN_ERROR_CODE(((key == NULL) || (value == NULL) || (ret_value == NULL)),
                                 "Input para contains NULL!");
    errno = 0;
    char *endptr = NULL;
    long result = strtol(value, &endptr, TEN_BASE);
    CHECK_COND_RETURN_ERROR_CODE(errno != 0, "Failed to load config: %s, value: %s, error message: %s.", key, value,
                                 strerror(errno));
    CHECK_COND_RETURN_ERROR_CODE((endptr == value), "Empty or non-numeric value for config: %s, value: %s.", key,
                                 value);
    CHECK_COND_RETURN_ERROR_CODE((*endptr != '\0'), "Invalid integer for config: %s, value: %s (trailing characters).",
                                 key, value);
    CHECK_COND_RETURN_ERROR_CODE((result < INT32_MIN || result > INT32_MAX),
                                 "Value out of int32 range for config: %s, value: %s.", key, value);
    CHECK_COND_RETURN_ERROR_CODE((result == -1), "Value of config: %s set to -1. This configuration will be ignored.",
                                 key);
    *ret_value = (int32_t)result;
    return ENPU_SUCCESS;
}

int load_str(const char *key, const char *value, char *ret_value, size_t ret_len)
{
    CHECK_COND_RETURN_ERROR_CODE(((key == NULL) || (value == NULL) || (ret_value == NULL)),
                                 "Input para contains NULL!");
    if (strlen(value) >= ret_len) {
        LOG_ERROR("Failed to load config: %s, value length (which is %lu)exceed buffer size %zu", key, strlen(value),
                  ret_len);
        return ENPU_FAIL;
    }

    int ret = strcpy_s(ret_value, ret_len, value);
    CHECK_COND_RETURN_ERROR_CODE(ret != 0, "Failed to load config: %s, string copy failed.", key);
    return ENPU_SUCCESS;
}

int load_uint64(const char *key, const char *value, uint64_t *ret_value)
{
    CHECK_COND_RETURN_ERROR_CODE(((key == NULL) || (value == NULL) || (ret_value == NULL)),
                                 "Input para contains NULL!");
    errno = 0;
    char *endptr = NULL;
    unsigned long long result = strtoull(value, &endptr, TEN_BASE);
    CHECK_COND_RETURN_ERROR_CODE(errno != 0, "Failed to load config: %s, value: %s, error message: %s.", key, value,
                                 strerror(errno));
    CHECK_COND_RETURN_ERROR_CODE((result == ULLONG_MAX), "Value out of range for config: %s, value: %s.", key, value);
    CHECK_COND_RETURN_ERROR_CODE((endptr == value), "Empty or non-numeric value for config: %s, value: %s.", key,
                                 value);
    CHECK_COND_RETURN_ERROR_CODE((*endptr != '\0'), "Invalid uint64 for config: %s, value: %s (trailing characters).",
                                 key, value);
    *ret_value = result;
    return ENPU_SUCCESS;
}

int save2config(const char *key, const char *value)
{
    int rc = ENPU_SUCCESS;
    if (strcmp(key, OPTION_NPU_ID) == 0) {
        rc = load_int32(key, value, &config.phy_npu_id);
    } else if (strcmp(key, OPTION_VNPU_ID) == 0) {
        rc = load_int32(key, value, &config.vnpu_id);
    } else if (strcmp(key, OPTION_AICORE_QUOTA) == 0) {
        rc = load_int32(key, value, &config.aicore_quota);
    } else if (strcmp(key, OPTION_MEMORY_QUOTA) == 0) {
        rc = load_int32(key, value, &config.memory_quota);
    } else if (strcmp(key, OPTION_MEMORY_REQUEST) == 0) {
        rc = load_uint64(key, value, &config.memory_request);
    } else if (strcmp(key, OPTION_MEMORY_LIMIT) == 0) {
        rc = load_uint64(key, value, &config.memory_limit);
    } else if (strcmp(key, OPTION_SCHEDULING_POLICY) == 0) {
        rc = load_int32(key, value, &config.scheduling_policy);
    } else if (strcmp(key, OPTION_VNPU_PRIORITY) == 0) {
        rc = load_int32(key, value, &config.vnpu_priority);
    } else if (strcmp(key, OPTION_SHM_ID) == 0) {
        rc = load_str(key, value, config.shm_id, SHM_ID_LEN);
    } else {
        LOG_WARN("Undefined config key: %s", key);
        rc = ENPU_FAIL;
    }
    return rc;
}

int load_config(const char *file_path)
{
    static char buffer[MAX_LINE_LENGTH];
    if (!file_path) {
        LOG_ERROR("Invalid input args: file_path is NULL");
        return ENPU_FAIL;
    }

    FILE *file = fopen(file_path, "r");
    CHECK_COND_RETURN_ERROR_CODE(!file, "Failed to open file: %s, error msg: %s.", file_path, strerror(errno));

    reset_config();

    while (fgets(buffer, sizeof(buffer), file) != NULL) {
        size_t pos = 0; // Cur position in the buffer
        size_t len = 0; // New length of the buffer
        while (buffer[pos] != '\0') {
            if (buffer[pos] < '!') { // Skip non-printable ASCII characters and spaces
                pos += 1;
                continue;
            }
            buffer[len++] = buffer[pos++];
        }
        buffer[len] = '\0';
        if (buffer[0] == '#' || buffer[0] == '\0') { // Commented line or empty line
            continue;
        }

        char *equal_pos = strchr(buffer, '='); // Found '=' in the line
        if (!equal_pos) {
            continue;
        }

        *equal_pos = '\0'; // Using '=' as the delimiter between key and value
        char *key = buffer;
        char *value = equal_pos + 1;

        int rc = save2config(key, value);
        if (rc == ENPU_SUCCESS) {
            LOG_INFO("Success to load config: %s, value: %s", key, value);
        } else {
            LOG_WARN("Failed to load config: %s, value: %s", key, value);
        }
    }
    if (fclose(file) != 0) {
        LOG_ERROR("Failed to close config file. Reason: %s", strerror(errno));
    }
    return check_config() ? ENPU_SUCCESS : ENPU_FAIL;
}
