/* Copyright (c) Huawei Technologies Co., Ltd. 2026. All rights reserved.
 * Licensed under Mulan PSL v2. */
#ifndef ENPU_RESOURCE_FORMAT_H
#define ENPU_RESOURCE_FORMAT_H
#include "enpu_manager.h"

#define ENPU_RESOURCE_BODY_LIMIT (8U * 1024U * 1024U)
/* Strict URI parser. No percent-encoded paths, duplicate or unknown parameters. */
int resource_parse_uri(const char *uri, enpu_resource_query_t *query, bool *metrics);
/* Heap output owned by caller; on failure *output is NULL. No partial results. */
int resource_format(const enpu_resource_response_t *response, bool metrics, char **output);
#endif
