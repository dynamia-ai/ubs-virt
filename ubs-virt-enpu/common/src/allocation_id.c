/*
 * Copyright (c) Huawei Technologies Co., Ltd. 2026. All rights reserved.
 * ubs-virt-enpu is licensed under Mulan PSL v2.
 */

#include "allocation_id.h"

#include <string.h>

bool allocation_id_is_valid(const char *allocation_id)
{
    if (allocation_id == NULL || strnlen(allocation_id, ALLOCATION_ID_LEN) != ALLOCATION_ID_STRING_LEN) {
        return false;
    }

    for (int i = 0; i < ALLOCATION_ID_STRING_LEN; ++i) {
        char c = allocation_id[i];
        if (i == 8 || i == 13 || i == 18 || i == 23) {
            if (c != '-') {
                return false;
            }
        } else if (!((c >= '0' && c <= '9') || (c >= 'a' && c <= 'f') || (c >= 'A' && c <= 'F'))) {
            return false;
        }
    }
    return true;
}

bool allocation_id_normalize(const char *allocation_id, char output[ALLOCATION_ID_LEN])
{
    if (output == NULL || !allocation_id_is_valid(allocation_id)) {
        return false;
    }
    for (int i = 0; i < ALLOCATION_ID_STRING_LEN; ++i) {
        char c = allocation_id[i];
        output[i] = c >= 'A' && c <= 'F' ? c + ('a' - 'A') : c;
    }
    output[ALLOCATION_ID_STRING_LEN] = '\0';
    return true;
}
