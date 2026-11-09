/*
 * Copyright (c) Huawei Technologies Co., Ltd. 2026. All rights reserved.
 * ubs-virt-enpu is licensed under Mulan PSL v2.
 */

#include "allocation_id.h"

#include <string.h>
#include <uuid/uuid.h>

bool allocation_id_is_valid(const char *allocation_id)
{
    if (allocation_id == NULL || strnlen(allocation_id, ALLOCATION_ID_LEN) != ALLOCATION_ID_STRING_LEN) {
        return false;
    }

    uuid_t uuid;
    return uuid_parse(allocation_id, uuid) == 0;
}
