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

#ifndef MCS_RUNTIME_STUB_H
#define MCS_RUNTIME_STUB_H

#include <stddef.h>
#include <stdint.h>

#if defined(__cplusplus)
extern "C" {
#endif

extern int g_stub_symbols_available;
extern int g_stub_set_device_return;
extern int g_stub_core_count_return;
extern int g_stub_device_info_return;
extern int g_stub_launch_return;
extern int g_stub_res_return;
extern int g_stub_malloc_return;
extern int g_stub_free_return;
extern int g_stub_set_device_calls;
extern uint32_t g_stub_physical_aicube;
extern uint32_t g_stub_physical_aivector;
extern uint32_t g_stub_res_value;

void reset_runtime_stub(void);

#if defined(__cplusplus)
}
#endif

#endif
