/*
 * Copyright (c) Huawei Technologies Co., Ltd. 2026. All rights reserved.
 * enpu-manager is licensed under Mulan PSL v2.
 */

#include "allocation.h"

#include <errno.h>
#include <sys/random.h>
#include "log.h"

int allocation_id_generate(char allocation_id[ALLOCATION_ID_LEN])
{
    if (allocation_id == NULL) {
        return ENPU_INVALID_PARAM;
    }

    unsigned char bytes[16];
    size_t filled = 0;
    while (filled < sizeof(bytes)) {
        ssize_t count = getrandom(bytes + filled, sizeof(bytes) - filled, 0);
        if (count < 0 && errno == EINTR) {
            continue;
        }
        if (count <= 0) {
            allocation_id[0] = '\0';
            LOG_ERROR("Failed to generate allocation_id: getrandom returned %zd, errno=%d", count, errno);
            return ENPU_FAIL;
        }
        filled += (size_t)count;
    }
    bytes[6] = (bytes[6] & 0x0f) | 0x40; /* UUID version 4. */
    bytes[8] = (bytes[8] & 0x3f) | 0x80; /* RFC UUID variant. */
    static const char hex[] = "0123456789abcdef";
    size_t offset = 0;
    for (size_t i = 0; i < sizeof(bytes); ++i) {
        if (i == 4 || i == 6 || i == 8 || i == 10) {
            allocation_id[offset++] = '-';
        }
        allocation_id[offset++] = hex[bytes[i] >> 4];
        allocation_id[offset++] = hex[bytes[i] & 0x0f];
    }
    allocation_id[offset] = '\0';
    return ENPU_SUCCESS;
}
