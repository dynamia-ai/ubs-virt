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

#ifndef __VNPU_UTILIZATION_CALCULATOR_H__
#define __VNPU_UTILIZATION_CALCULATOR_H__

#include <stdint.h>

static inline double vnpu_utilization_work(uint64_t sum_block_dim, uint64_t avg_kernel_duration_ns)
{
    return (double)sum_block_dim * (double)avg_kernel_duration_ns;
}

static inline double vnpu_utilization_weight(double work, double total_work)
{
    return (total_work > 0.0) ? work / total_work : 0.0;
}

static inline double vnpu_utilization_share(double physical_utilization, double weight)
{
    return physical_utilization * weight;
}

#endif /* __VNPU_UTILIZATION_CALCULATOR_H__ */
