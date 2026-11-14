/*
 * Copyright (c) Huawei Technologies Co., Ltd. 2026-2026. All rights reserved.
 * ubs-virt-enpu is licensed under Mulan PSL v2.
 * You can use this software according to the terms and conditions of the Mulan PSL v2.
 * You may obtain a copy of the License at:
 *          http://license.coscl.org.cn/MulanPSL2
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND,
 * EITHER EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT,
 * MERCHANTABILITY OR FIT FOR A PARTICULAR PURPOSE.
 * See the Mulan PSL v2 for more details.
 */

#ifndef __VNPU_METRICS_READER_H__
#define __VNPU_METRICS_READER_H__

#include <stdint.h>
#include "vnpu_stats_hbm.h"
#include "vnpu_stats_shm_abi.h"

typedef enum vnpu_stats_reader_status
{
    VNPU_STATS_READER_OK = 0,
    VNPU_STATS_READER_MISSING,
    VNPU_STATS_READER_VERSION_MISMATCH,
    VNPU_STATS_READER_IO_ERROR,
} vnpu_stats_reader_status_t;

typedef struct vnpu_raw_metric {
    uint64_t sum_block_dim;
    uint64_t launch_count;
    uint64_t avg_kernel_duration_ns;
} vnpu_raw_metric_t;

typedef struct vnpu_stats_snapshot {
    uint64_t collected_at_ns;
    vnpu_raw_metric_t metrics[VNPU_STATS_MAX_VNPU];
    vnpu_hbm_snapshot_t hbm[VNPU_STATS_MAX_VNPU];
} vnpu_stats_snapshot_t;

#if defined(__cplusplus)
extern "C" {
#endif

vnpu_stats_reader_status_t vnpu_metrics_reader_snapshot(const char *base_shm_id, vnpu_stats_snapshot_t *snapshot);

#if defined(__cplusplus)
}
#endif

#endif /* __VNPU_METRICS_READER_H__ */
