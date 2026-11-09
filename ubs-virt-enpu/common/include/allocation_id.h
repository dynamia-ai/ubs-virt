/*
 * Copyright (c) Huawei Technologies Co., Ltd. 2026. All rights reserved.
 * ubs-virt-enpu is licensed under Mulan PSL v2.
 */

#ifndef __ENPU_ALLOCATION_ID_H__
#define __ENPU_ALLOCATION_ID_H__

#include <stdbool.h>

#define ALLOCATION_ID_STRING_LEN 36
#define ALLOCATION_ID_LEN (ALLOCATION_ID_STRING_LEN + 1)

#if defined(__cplusplus)
extern "C" {
#endif

bool allocation_id_is_valid(const char *allocation_id);

#if defined(__cplusplus)
}
#endif

#endif
