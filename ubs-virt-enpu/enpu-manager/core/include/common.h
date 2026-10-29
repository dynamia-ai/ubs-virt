/*
 * Copyright (c) Huawei Technologies Co., Ltd. 2025-2026. All rights reserved.
 * enpu-manager is licensed under Mulan PSL v2.
 * You can use this software according to the terms and conditions of the Mulan PSL v2.
 * You may obtain a copy of Mulan PSL v2 at:
 *          http://license.coscl.org.cn/MulanPSL2
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND,
 * EITHER EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT,
 * MERCHANTABILITY OR FIT FOR A PARTICULAR PURPOSE.
 * See the Mulan PSL v2 for more details.
 */

#ifndef __ENPU_COMMON_H__
#define __ENPU_COMMON_H__

#include <stdatomic.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>

#define ENPU_SUCCESS 0
// 此处宏定义和vcann-rt组件有差异，不会发生冲突，显存超分场景更复杂，宏定义更多
#define ENPU_FAIL (-1)
#define ENPU_INVALID_PARAM (-2)
#define ENPU_NO_RESOURCE (-3)
#define ENPU_ALREADY_EXISTS (-4)
#define ENPU_NOT_FOUND (-5)

#define MAX_NPU_PER_NODE 16
#define MAX_VNPU_PER_DIE 128
#define HUNDRED_CORE 100
#define VNPU_PRIORITY_NORMAL 0
#define VNPU_PRIORITY_HIGH 1
#define MAX_ALLOC_COUNT 1024
#define DEFAULT_CHECKPOINT_PATH "/var/lib/enpu-manager/checkpoint.json"
#define MAX_PATH_LEN 256
#define MAX_UUID_LEN 64
#define DIE_ID_LEN 64
#define MAX_NAME_LEN 32
#define SHM_ID_LEN DIE_ID_LEN
#define MAX_CHILDREN 16

typedef enum
{
    SCHED_POLICY_FIXED_SHARE = 1,
    SCHED_POLICY_ELASTIC = 2,
    SCHED_POLICY_BEST_EFFORT = 3,
} schedule_policy_t;

/* swap_priority值越大越优先被换出 */
typedef enum
{
    SWAP_PRIORITY_HIGH = 0,
    SWAP_PRIORITY_MEDIUM = 1,
    SWAP_PRIORITY_LOW = 2,
} swap_priority_t;

typedef enum
{
    NPU_TOPO_SYS = 0,
    NPU_TOPO_CPU = 1,
    NPU_TOPO_CARD = 2,
    NPU_TOPO_DIE = 3,
} npu_topo_level_t;

typedef struct npu_meta {
    int32_t id;
    int32_t phy_id;
    char die_id[DIE_ID_LEN];
    int32_t minor_id;
    char uuid[MAX_UUID_LEN];
    char minor_name[MAX_NAME_LEN];
    uint64_t total_memory;
    char card_type[MAX_NAME_LEN];
} npu_meta_t;

typedef struct npu_allocatable {
    atomic_int aicore_quota;
    atomic_uint_fast64_t hbm_quota;
    atomic_int vnpu_count;
    uint64_t vnpu_mask;
} npu_allocatable_t;

#define unlikely(x) __builtin_expect(!!(x), 0)

#define CHECK_RETURN_RANGE_INT(val, min, max)                                                                          \
    do {                                                                                                               \
        if (((val) < (min)) || ((val) > (max))) {                                                                      \
            LOG_ERROR("Failed to load param [%s]: [%d] must be in range of [%d] and [%d]", #val, (val), (min), (max)); \
            return ENPU_FAIL;                                                                                          \
        }                                                                                                              \
    } while (false)

/// DO NOT PASS FUNCTION IN
#define CHECK_RETURN_ERROR_CODE(err, error_msg, ...) \
    do {                                             \
        if (unlikely((err) != ENPU_SUCCESS)) {       \
            LOG_ERROR(error_msg, ##__VA_ARGS__);     \
            return (err);                            \
        }                                            \
    } while (false)

#define CHECK_RETURN_ERROR_CODE_LOG(err, error_msg, ...)    \
    do {                                                    \
        if (unlikely((err) != ENPU_SUCCESS)) {              \
            fprintf(stderr, error_msg "\n", ##__VA_ARGS__); \
            return (err);                                   \
        }                                                   \
    } while (false)

#define CHECK_ERROR_CODE(err, error_msg, ...)    \
    do {                                         \
        if (unlikely((err) != ENPU_SUCCESS)) {   \
            LOG_ERROR(error_msg, ##__VA_ARGS__); \
        }                                        \
    } while (false)

#define CHECK_ERROR_CODE_LOG(err, error_msg, ...)           \
    do {                                                    \
        if (unlikely((err) != ENPU_SUCCESS)) {              \
            fprintf(stderr, error_msg "\n", ##__VA_ARGS__); \
        }                                                   \
    } while (false)

#define CHECK_COND_LOG_PRINT(err, error_msg, ...)           \
    do {                                                    \
        if (unlikely((err) != ENPU_SUCCESS)) {              \
            fprintf(stderr, error_msg "\n", ##__VA_ARGS__); \
        }                                                   \
    } while (false)

#define CHECK_COND_RETURN(cond, error_msg, ...)  \
    do {                                         \
        if (unlikely(cond)) {                    \
            LOG_ERROR(error_msg, ##__VA_ARGS__); \
            return;                              \
        }                                        \
    } while (false)

#define CHECK_COND_RETURN_LOG(cond, error_msg, ...)         \
    do {                                                    \
        if (unlikely(cond)) {                               \
            fprintf(stderr, error_msg "\n", ##__VA_ARGS__); \
            return;                                         \
        }                                                   \
    } while (false)

#define CHECK_COND_RETURN_ERROR_CODE(cond, error_msg, ...) \
    do {                                                   \
        if (unlikely(cond)) {                              \
            LOG_ERROR(error_msg, ##__VA_ARGS__);           \
            return ENPU_FAIL;                              \
        }                                                  \
    } while (false)

#define CHECK_COND_RETURN_ERROR_CODE_LOG(cond, error_msg, ...) \
    do {                                                       \
        if (unlikely(cond)) {                                  \
            fprintf(stderr, error_msg "\n", ##__VA_ARGS__);    \
            return ENPU_FAIL;                                  \
        }                                                      \
    } while (false)

#define CHECK_COND_RETURN_(cond, rt, error_msg, ...) \
    do {                                             \
        if (unlikely(cond)) {                        \
            LOG_ERROR(error_msg, ##__VA_ARGS__);     \
            return rt;                               \
        }                                            \
    } while (false)

#define CHECK_COND_LOG_(cond, error_msg, ...)    \
    do {                                         \
        if (unlikely(cond)) {                    \
            LOG_ERROR(error_msg, ##__VA_ARGS__); \
        }                                        \
    } while (false)

#endif
