/* Copyright (c) Huawei Technologies Co., Ltd. 2026. All rights reserved.
 * Licensed under Mulan PSL v2. */
#include <assert.h>
#include <stdio.h>
#include "swap_resolver.h"

int main(void)
{
    /* Selection uses only the supplied candidates; no manager or device is needed. */
    npu_allocator_t allocator = {0};
    int buffer_token = 0;
    swap_resolver_t *resolver = swap_resolver_create(&allocator, (swap_buffer_manager_t *)&buffer_token, 0.8, 100);
    assert(resolver != NULL);
    swap_candidate_t candidates[] = {
        {.vnpu_id = 2, .swap_order = 2, .last_kernel_time_ns = 1},
        {.vnpu_id = 1, .swap_order = 1, .last_kernel_time_ns = 2},
        {.vnpu_id = 0, .swap_order = 0, .last_kernel_time_ns = 100},
    };
    swap_candidate_t selected = {0};
    assert(swap_resolver_select_candidate_from_list(resolver, candidates, 3, &selected) == ENPU_SUCCESS);
    assert(selected.vnpu_id == 0); /* Order precedes idle time. */
    assert(swap_resolver_select_candidate_from_list(resolver, candidates, 2, &selected) == ENPU_SUCCESS);
    assert(selected.vnpu_id == 1);
    assert(swap_resolver_select_candidate_from_list(resolver, candidates, 1, &selected) == ENPU_SUCCESS);
    assert(selected.vnpu_id == 2);
    for (int order = 0; order <= 2; ++order) {
        candidates[0].swap_order = candidates[1].swap_order = order;
        candidates[0].last_kernel_time_ns = 20;
        candidates[1].last_kernel_time_ns = 10;
        assert(swap_resolver_select_candidate_from_list(resolver, candidates, 2, &selected) == ENPU_SUCCESS);
        assert(selected.vnpu_id == 1); /* Same order: longest idle first. */
    }
    assert(swap_resolver_destroy(resolver) == ENPU_SUCCESS);
    puts("swap order selection checks passed");
    return 0;
}
