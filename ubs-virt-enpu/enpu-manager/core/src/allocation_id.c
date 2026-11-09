/*
 * Copyright (c) Huawei Technologies Co., Ltd. 2026. All rights reserved.
 * enpu-manager is licensed under Mulan PSL v2.
 */

#include "allocation.h"

#include <uuid/uuid.h>

int allocation_id_generate(char allocation_id[ALLOCATION_ID_LEN])
{
    if (allocation_id == NULL) {
        return ENPU_INVALID_PARAM;
    }

    uuid_t uuid;
    uuid_generate_random(uuid);
    uuid_unparse_lower(uuid, allocation_id);
    return ENPU_SUCCESS;
}
