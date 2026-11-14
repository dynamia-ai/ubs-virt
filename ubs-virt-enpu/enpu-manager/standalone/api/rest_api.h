/*
 * Copyright (c) Huawei Technologies Co., Ltd. 2025-2026. All rights reserved.
 * enpu-manager is licensed under Mulan PSL v2.
 * You can use this software according to the terms and conditions of the Mulan PSL v2.
 * You may obtain a copy of Mulan PSL v2 at:
 *          http://license.coscl.org.cn/MulanPSL2
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND,
 * EITHER EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT,
 * MERCHANTABILITY OR FIT FOR A PARTICULAR PURPOSE.
 * See the Mulan PSL v2 for more details.
 */

#ifndef __REST_API_H__
#define __REST_API_H__

#include "enpu_manager.h"

typedef struct rest_api_server rest_api_server_t;

#if defined(__cplusplus)
extern "C" {
#endif

rest_api_server_t *rest_api_create(const char *socket_path);
int rest_api_destroy(rest_api_server_t *server);
int rest_api_start(rest_api_server_t *server);
int rest_api_stop(rest_api_server_t *server);
void rest_api_set_manager(rest_api_server_t *server, enpu_manager_t *mgr);

#if defined(__cplusplus)
}
#endif

#endif
