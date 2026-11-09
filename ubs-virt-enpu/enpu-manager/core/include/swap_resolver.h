/*
 * Copyright (c) Huawei Technologies Co., Ltd. 2025-2026. All rights reserved.
 * enpu-manager is licensed under Mulan PSL v2.
 */

#ifndef __SWAP_RESOLVER_H__
#define __SWAP_RESOLVER_H__

#include <stdbool.h>
#include <stdint.h>
#include "common.h"
#include "npu_allocator_internal.h"
#include "swap_buffer_manager.h"

struct npu_allocator;

typedef enum
{
    WATERMARK_NORMAL = 0,
    WATERMARK_PRE_SWAP = 1,
} watermark_level_t;

typedef struct swap_candidate {
    int phy_id;
    char shm_id[DIE_ID_LEN];
    int vnpu_id;
    int sched_policy;
    int swap_order;
    uint64_t last_kernel_time_ns;
    uint64_t hbm_used;
    bool swap_enabled;
    bool swapped;
    bool swappable;
} swap_candidate_t;

typedef struct swap_resolver swap_resolver_t;

#if defined(__cplusplus)
extern "C" {
#endif

swap_resolver_t *swap_resolver_create(npu_allocator_t *allocator, swap_buffer_manager_t *swap_buf_mgr,
                                      double pre_swap_threshold, uint64_t idle_threshold_ns);
int swap_resolver_destroy(swap_resolver_t *resolver);

watermark_level_t swap_resolver_check_watermark(swap_resolver_t *resolver, int phy_id);
int swap_resolver_get_idle_models(swap_resolver_t *resolver, int phy_id, swap_candidate_t *candidates, int max_count,
                                  int *actual_count, uint8_t flag);
int swap_resolver_select_candidate(swap_resolver_t *resolver, int phy_id, swap_candidate_t *selected);
int swap_resolver_select_candidate_from_list(swap_resolver_t *resolver, swap_candidate_t *candidates, int count,
                                             swap_candidate_t *selected);
int swap_resolver_write_swap_command(swap_resolver_t *resolver, int phy_id, const char *pod_uid, int vnpu_id,
                                     int action, uint8_t flag);
int swap_resolver_check_and_swap_in(swap_resolver_t *resolver, int phy_id);
int swap_resolver_check_enpu_swap_out(swap_resolver_t *resolver, int phy_id, uint8_t *flag);

/* 检查上一次 swap_out 命令是否仍在执行中（vcann-rt 尚未 ack completed）*/
bool swap_resolver_is_swap_pending(swap_resolver_t *resolver, int phy_id);

#if defined(__cplusplus)
}
#endif

#endif
