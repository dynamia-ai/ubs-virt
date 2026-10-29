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

#include "rest_api.h"
#include <arpa/inet.h>
#include <errno.h>
#include <netinet/in.h>
#include <pthread.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <time.h>
#include <unistd.h>
#include "../../core/include/allocation.h"
#include "../../core/include/common.h"
#include "../../core/include/enpu_config.h"
#include "../../core/include/enpu_manager.h"
#include "../../core/include/log.h"
#include "../../core/include/swap_buffer_manager.h"
#include "securec.h"

#define REST_MAX_CONNECTIONS 10
#define REST_BUFFER_SIZE 4096
#define REST_RESPONSE_SIZE 65536
#define DEVICE_JSON_BUF_SIZE 512
#define REST_RECV_TIMEOUT_SEC 5
#define HTTP_STATUS_OK 200
#define HTTP_STATUS_BAD_REQUEST 400
#define HTTP_STATUS_NOT_FOUND 404
#define HTTP_STATUS_INTERNAL_ERROR 500

#define HTTP_STATUS_METHOD_NOT_ALLOWED 405

typedef struct rest_api_server {
    char bind_addr[64];
    int port;
    int server_fd;
    int running;
    pthread_t accept_thread;
    enpu_manager_t *manager;
    pthread_mutex_t lock;
} rest_api_server_t;

static int parse_http_request(char *buffer, char *method, size_t method_size, char *path, size_t path_size, char *body,
                              size_t body_size);
static int handle_request(rest_api_server_t *server, int client_fd);
static void *accept_thread_func(void *arg);
static int send_response(int client_fd, int status, const char *body);
static int route_request(rest_api_server_t *server, const char *method, const char *path, const char *body,
                         char *response);

static char *format_devices_json(enpu_manager_t *mgr);
static char *format_device_json(enpu_manager_t *mgr, int phy_id);
static char *format_allocations_json(enpu_manager_t *mgr);
static int parse_json_alloc_request(const char *body, alloc_request_t *req);

static int is_decimal_string(const char *s)
{
    if (s == NULL || *s == '\0') {
        return 0;
    }
    for (const char *p = s; *p != '\0'; p++) {
        if (*p < '0' || *p > '9') {
            return 0;
        }
    }
    return 1;
}

static const char *json_number_value(const char *json, const char *key)
{
    const char *p = strstr(json, key);
    if (p == NULL) {
        return NULL;
    }
    const char *colon = strchr(p, ':');
    if (colon == NULL) {
        return NULL;
    }
    return colon + 1;
}

static int is_safe_key(const char *s)
{
    for (const char *p = s; *p != '\0'; p++) {
        if ((unsigned char)*p < 0x20 || *p == '"' || *p == '\\') {
            return 0;
        }
    }
    return 1;
}

static const char *http_reason_phrase(int status)
{
    switch (status) {
        case HTTP_STATUS_OK:
            return "OK";
        case HTTP_STATUS_BAD_REQUEST:
            return "Bad Request";
        case HTTP_STATUS_NOT_FOUND:
            return "Not Found";
        case HTTP_STATUS_METHOD_NOT_ALLOWED:
            return "Method Not Allowed";
        case HTTP_STATUS_INTERNAL_ERROR:
            return "Internal Server Error";
        default:
            return "OK";
    }
}

rest_api_server_t *rest_api_create(const char *bind_addr)
{
    rest_api_server_t *server = calloc(1, sizeof(rest_api_server_t));
    if (!server)
        return NULL;

    if (bind_addr) {
        const char *colon = strchr(bind_addr, ':');
        if (colon) {
            size_t ip_len = colon - bind_addr;
            if (ip_len > sizeof(server->bind_addr) - 1) {
                ip_len = sizeof(server->bind_addr) - 1;
            }
            int ret = memcpy_s(server->bind_addr, sizeof(server->bind_addr), bind_addr, ip_len);
            CHECK_COND_LOG_(ret != 0, "memcpy_s bind_addr failed.");
            server->bind_addr[ip_len] = '\0';
            server->port = atoi(colon + 1);
        } else {
            int ret = strncpy_s(server->bind_addr, sizeof(server->bind_addr), bind_addr, sizeof(server->bind_addr) - 1);
            CHECK_COND_LOG_(ret != 0, "strncpy_s bind_addr failed.");
            server->port = ENPU_CONFIG_DEFAULT_REST_PORT;
        }
    } else {
        int ret = strncpy_s(server->bind_addr, sizeof(server->bind_addr), "0.0.0.0", sizeof("0.0.0.0") - 1);
        CHECK_COND_LOG_(ret != 0, "strncpy_s bind_addr failed.");
        server->port = ENPU_CONFIG_DEFAULT_REST_PORT;
    }
    server->server_fd = -1;
    server->running = 0;
    server->manager = NULL;
    pthread_mutex_init(&server->lock, NULL);

    return server;
}

int rest_api_destroy(rest_api_server_t *server)
{
    if (!server)
        return ENPU_INVALID_PARAM;

    rest_api_stop(server);
    pthread_mutex_destroy(&server->lock);
    free(server);
    return ENPU_SUCCESS;
}
void rest_api_set_manager(rest_api_server_t *server, enpu_manager_t *mgr)
{
    if (!server)
        return;
    pthread_mutex_lock(&server->lock);
    server->manager = mgr;
    pthread_mutex_unlock(&server->lock);
}
int rest_api_start(rest_api_server_t *server)
{
    if (!server)
        return ENPU_INVALID_PARAM;

    pthread_mutex_lock(&server->lock);
    if (server->running) {
        pthread_mutex_unlock(&server->lock);
        return ENPU_ALREADY_EXISTS;
    }

    struct sockaddr_in addr;
    int ret = memset_s(&addr, sizeof(addr), 0, sizeof(addr));
    if (ret != 0) {
        pthread_mutex_unlock(&server->lock);
        LOG_ERROR("[REST] memset_s addr failed: %d", ret);
        return ENPU_FAIL;
    }
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = inet_addr(server->bind_addr);
    addr.sin_port = htons(server->port);

    server->server_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (server->server_fd < 0) {
        pthread_mutex_unlock(&server->lock);
        LOG_ERROR("[REST] Failed to create socket: %s", strerror(errno));
        return ENPU_FAIL;
    }

    if (bind(server->server_fd, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
        close(server->server_fd);
        server->server_fd = -1;
        pthread_mutex_unlock(&server->lock);
        LOG_ERROR("[REST] Failed to bind socket: %s", strerror(errno));
        return ENPU_FAIL;
    }

    if (listen(server->server_fd, REST_MAX_CONNECTIONS) < 0) {
        close(server->server_fd);
        server->server_fd = -1;
        pthread_mutex_unlock(&server->lock);
        LOG_ERROR("[REST] Failed to listen socket: %s", strerror(errno));
        return ENPU_FAIL;
    }

    server->running = 1;
    int create_ret = pthread_create(&server->accept_thread, NULL, accept_thread_func, server);
    if (create_ret != 0) {
        server->running = 0;
        close(server->server_fd);
        server->server_fd = -1;
        pthread_mutex_unlock(&server->lock);
        LOG_ERROR("[REST] Failed to create accept thread: %d", create_ret);
        return ENPU_FAIL;
    }
    pthread_mutex_unlock(&server->lock);

    LOG_INFO("[REST] Server started on %s:%d", server->bind_addr, server->port);
    return ENPU_SUCCESS;
}
int rest_api_stop(rest_api_server_t *server)
{
    if (!server)
        return ENPU_INVALID_PARAM;

    pthread_mutex_lock(&server->lock);
    if (!server->running) {
        pthread_mutex_unlock(&server->lock);
        return ENPU_SUCCESS;
    }

    server->running = 0;
    if (server->server_fd >= 0) {
        shutdown(server->server_fd, SHUT_RDWR);
        close(server->server_fd);
        server->server_fd = -1;
    }

    pthread_mutex_unlock(&server->lock);

    pthread_join(server->accept_thread, NULL);
    LOG_INFO("[REST] Server stopped");
    return ENPU_SUCCESS;
}
static void *accept_thread_func(void *arg)
{
    rest_api_server_t *server = (rest_api_server_t *)arg;
    struct sockaddr_in client_addr;
    socklen_t client_len = sizeof(client_addr);

    if (signal(SIGPIPE, SIG_IGN) == SIG_ERR) {
        LOG_ERROR("[REST] Failed to ignore SIGPIPE.");
    }
    while (server->running) {
        int client_fd = accept(server->server_fd, (struct sockaddr *)&client_addr, &client_len);
        if (client_fd < 0) {
            if (errno == EINTR)
                continue;
            usleep(100000);
            continue;
        }
        handle_request(server, client_fd);
        close(client_fd);
    }
    return NULL;
}
static int handle_request(rest_api_server_t *server, int client_fd)
{
    char buffer[REST_BUFFER_SIZE];
    char method[16], path[256], body[REST_BUFFER_SIZE];
    struct timeval recv_timeout = {REST_RECV_TIMEOUT_SEC, 0};
    int sock_ret = setsockopt(client_fd, SOL_SOCKET, SO_RCVTIMEO, &recv_timeout, sizeof(recv_timeout));
    CHECK_COND_LOG_(sock_ret != 0, "setsockopt SO_RCVTIMEO failed.");
    int bytes_read = read(client_fd, buffer, REST_BUFFER_SIZE - 1);
    if (bytes_read <= 0)
        return ENPU_FAIL;
    buffer[bytes_read] = '\0';

    if (parse_http_request(buffer, method, sizeof(method), path, sizeof(path), body, sizeof(body)) != 0) {
        send_response(client_fd, HTTP_STATUS_BAD_REQUEST, "{\"error\":\"Invalid HTTP request\"}");
        return ENPU_FAIL;
    }

    char *response = (char *)malloc(REST_RESPONSE_SIZE);
    if (response == NULL) {
        send_response(client_fd, HTTP_STATUS_INTERNAL_ERROR, "{\"error\":\"OOM\"}");
        return ENPU_FAIL;
    }

    int result = route_request(server, method, path, body, response);

    send_response(client_fd, result, response);
    free(response);
    return ENPU_SUCCESS;
}
static int parse_http_request(char *buffer, char *method, size_t method_size, char *path, size_t path_size, char *body,
                              size_t body_size)
{
    char *line_end = strstr(buffer, "\r\n");
    if (!line_end)
        return -1;
    *line_end = '\0';
    char *space = strchr(buffer, ' ');
    if (!space)
        return -1;
    size_t method_len = (size_t)(space - buffer);
    if (method_len >= method_size)
        return -1;
    int copy_ret = strncpy_s(method, method_size, buffer, method_len);
    CHECK_COND_RETURN_(copy_ret != 0, -1, "strncpy_s method failed.");
    char *path_start = space + 1;
    char *path_end = strchr(path_start, ' ');
    if (!path_end)
        return -1;
    size_t path_len = (size_t)(path_end - path_start);
    if (path_len >= path_size)
        return -1;
    copy_ret = strncpy_s(path, path_size, path_start, path_len);
    CHECK_COND_RETURN_(copy_ret != 0, -1, "strncpy_s path failed.");

    char *body_start = strstr(line_end + 2, "\r\n\r\n");
    if (body_start) {
        copy_ret = strncpy_s(body, body_size, body_start + 4, body_size - 1);
        CHECK_COND_RETURN_(copy_ret != 0, -1, "strncpy_s body failed.");
    } else {
        body[0] = '\0';
    }
    return 0;
}
static int send_response(int client_fd, int status, const char *body)
{
    char header[512];
    size_t body_len = strlen(body);

    int header_len = snprintf_s(header, sizeof(header), sizeof(header) - 1,
                                "HTTP/1.1 %d %s\r\n"
                                "Content-Type: application/json\r\n"
                                "Content-Length: %zu\r\n"
                                "Connection: close\r\n"
                                "\r\n",
                                status, http_reason_phrase(status), body_len);
    CHECK_COND_RETURN_(header_len < 0, ENPU_FAIL, "snprintf_s header failed.");

    ssize_t written = 0;
    while (written < header_len) {
        ssize_t n = write(client_fd, header + written, header_len - written);
        if (n <= 0)
            break;
        written += n;
    }

    written = 0;
    while (written < (ssize_t)body_len) {
        ssize_t n = write(client_fd, body + written, body_len - written);
        if (n <= 0)
            break;
        written += n;
    }

    return 0;
}
static int route_request(rest_api_server_t *server, const char *method, const char *path, const char *body,
                         char *response)
{
    pthread_mutex_lock(&server->lock);
    enpu_manager_t *mgr = server->manager;
    pthread_mutex_unlock(&server->lock);

    int ret = 0;

    if (!mgr) {
        ret = snprintf_s(response, REST_RESPONSE_SIZE, REST_RESPONSE_SIZE - 1,
                         "{\"error\":\"Manager not initialized\",\"result\":%d}", ENPU_FAIL);
        CHECK_COND_LOG_(ret < 0, "snprintf_s response failed.");
        return HTTP_STATUS_INTERNAL_ERROR;
    }

    if (strcmp(method, "GET") == 0 && strcmp(path, "/api/v1/devices") == 0) {
        char *json = format_devices_json(mgr);
        if (json) {
            ret = snprintf_s(response, REST_RESPONSE_SIZE, REST_RESPONSE_SIZE - 1, "%s", json);
            CHECK_COND_LOG_(ret < 0, "snprintf_s response failed.");
            free(json);
            return HTTP_STATUS_OK;
        }
        ret = snprintf_s(response, REST_RESPONSE_SIZE, REST_RESPONSE_SIZE - 1,
                         "{\"error\":\"Failed to list devices\",\"result\":%d}", ENPU_FAIL);
        CHECK_COND_LOG_(ret < 0, "snprintf_s response failed.");
        return HTTP_STATUS_INTERNAL_ERROR;
    }

    if (strcmp(method, "GET") == 0 && strncmp(path, "/api/v1/devices/", 16) == 0) {
        long phy_id = is_decimal_string(path + 16) ? strtol(path + 16, NULL, 10) : -1;
        if (phy_id < 0 || phy_id >= MAX_NPU_PER_NODE) {
            ret = snprintf_s(response, REST_RESPONSE_SIZE, REST_RESPONSE_SIZE - 1,
                             "{\"error\":\"Invalid device id\",\"result\":%d}", ENPU_INVALID_PARAM);
            CHECK_COND_LOG_(ret < 0, "snprintf_s response failed.");
            return HTTP_STATUS_BAD_REQUEST;
        }
        char *json = format_device_json(mgr, (int)phy_id);
        if (json) {
            ret = snprintf_s(response, REST_RESPONSE_SIZE, REST_RESPONSE_SIZE - 1, "%s", json);
            CHECK_COND_LOG_(ret < 0, "snprintf_s response failed.");
            free(json);
            return HTTP_STATUS_OK;
        }
        ret = snprintf_s(response, REST_RESPONSE_SIZE, REST_RESPONSE_SIZE - 1,
                         "{\"error\":\"Device not found\",\"result\":%d}", ENPU_NOT_FOUND);
        CHECK_COND_LOG_(ret < 0, "snprintf_s response failed.");
        return HTTP_STATUS_NOT_FOUND;
    }

    if (strcmp(method, "POST") == 0 && strcmp(path, "/api/v1/allocate") == 0) {
        alloc_request_t req;
        alloc_response_t resp = {0};
        if (parse_json_alloc_request(body, &req) != 0) {
            ret = snprintf_s(response, REST_RESPONSE_SIZE, REST_RESPONSE_SIZE - 1,
                             "{\"error\":\"Invalid request body\",\"result\":%d}", ENPU_INVALID_PARAM);
            CHECK_COND_LOG_(ret < 0, "snprintf_s response failed.");
            return HTTP_STATUS_BAD_REQUEST;
        }
        int result = enpu_manager_allocate(mgr, &req, &resp);
        if (result != ENPU_SUCCESS) {
            resp.result = result; /* 用返回值覆盖 resp.result，避免 rollback 后 resp.result 仍为 SUCCESS */
        }
        ret = snprintf_s(response, REST_RESPONSE_SIZE, REST_RESPONSE_SIZE - 1,
                         "{\"phy_id\":%d,\"vnpu_id\":%d,\"pod_uid\":\"%s\",\"container_name\":\"%s\","
                         "\"die_id\":\"%s\",\"shm_id\":\"%s\","
                         "\"aicore_quota\":%d,\"hbm_quota\":%lu,\"hbm_limit\":%lu,\"sched_policy\":%d,"
                         "\"vnpu_priority\":%d,"
                         "\"minor_name\":\"%s\",\"result\":%d,\"error_msg\":\"%s\"}",
                         resp.phy_id, resp.vnpu_id, resp.pod_uid, resp.container_name, resp.die_id, resp.shm_id,
                         resp.aicore_quota, resp.hbm_quota, resp.hbm_limit, resp.sched_policy, resp.vnpu_priority,
                         resp.minor_name,
                         resp.result, resp.error_msg);
        CHECK_COND_LOG_(ret < 0, "snprintf_s response failed.");
        return result == ENPU_SUCCESS ? HTTP_STATUS_OK : HTTP_STATUS_BAD_REQUEST;
    }

    if (strcmp(method, "DELETE") == 0 && strncmp(path, "/api/v1/allocations/", 20) == 0) {
        char *slash = strchr(path + 20, '/');
        if (!slash) {
            ret = snprintf_s(response, REST_RESPONSE_SIZE, REST_RESPONSE_SIZE - 1,
                             "{\"error\":\"Invalid path\",\"result\":%d}", ENPU_INVALID_PARAM);
            CHECK_COND_LOG_(ret < 0, "snprintf_s response failed.");
            return HTTP_STATUS_BAD_REQUEST;
        }
        char pod_uid[MAX_UUID_LEN] = {0}, container[MAX_NAME_LEN] = {0};
        size_t uid_len = (size_t)(slash - (path + 20));
        if (uid_len >= sizeof(pod_uid)) {
            uid_len = sizeof(pod_uid) - 1;
        }
        int copy_ret = strncpy_s(pod_uid, sizeof(pod_uid), path + 20, uid_len);
        CHECK_COND_RETURN_ERROR_CODE_LOG(copy_ret != 0, "strncpy_s pod_uid failed.");
        copy_ret = strncpy_s(container, sizeof(container), slash + 1, sizeof(container) - 1);
        CHECK_COND_RETURN_ERROR_CODE_LOG(copy_ret != 0, "strncpy_s container failed.");
        int result = enpu_manager_release(mgr, pod_uid, container);
        ret = snprintf_s(response, REST_RESPONSE_SIZE, REST_RESPONSE_SIZE - 1, "{\"result\":%d,\"error_msg\":\"\"}",
                         result);
        CHECK_COND_LOG_(ret < 0, "snprintf_s response failed.");
        return result == ENPU_SUCCESS ? HTTP_STATUS_OK : HTTP_STATUS_NOT_FOUND;
    }

    if (strcmp(method, "GET") == 0 && strcmp(path, "/api/v1/allocations") == 0) {
        char *json = format_allocations_json(mgr);
        if (json) {
            ret = snprintf_s(response, REST_RESPONSE_SIZE, REST_RESPONSE_SIZE - 1, "%s", json);
            CHECK_COND_LOG_(ret < 0, "snprintf_s response failed.");
            free(json);
            return HTTP_STATUS_OK;
        }
        ret = snprintf_s(response, REST_RESPONSE_SIZE, REST_RESPONSE_SIZE - 1,
                         "{\"error\":\"Failed to list allocations\",\"result\":%d}", ENPU_FAIL);
        CHECK_COND_LOG_(ret < 0, "snprintf_s response failed.");
        return HTTP_STATUS_INTERNAL_ERROR;
    }

    if (strcmp(method, "POST") == 0 && strcmp(path, "/api/v1/checkpoint") == 0) {
        int result = enpu_manager_control(mgr, CONTROL_OP_CHECKPOINT);
        ret = snprintf_s(response, REST_RESPONSE_SIZE, REST_RESPONSE_SIZE - 1, "{\"result\":%d,\"error_msg\":\"\"}",
                         result);
        CHECK_COND_LOG_(ret < 0, "snprintf_s response failed.");
        return result == ENPU_SUCCESS ? HTTP_STATUS_OK : HTTP_STATUS_INTERNAL_ERROR;
    }

    if (strcmp(method, "POST") == 0 && strcmp(path, "/api/v1/recover") == 0) {
        int result = enpu_manager_control(mgr, CONTROL_OP_RECOVER);
        ret = snprintf_s(response, REST_RESPONSE_SIZE, REST_RESPONSE_SIZE - 1, "{\"result\":%d,\"error_msg\":\"\"}",
                         result);
        CHECK_COND_LOG_(ret < 0, "snprintf_s response failed.");
        return result == ENPU_SUCCESS ? HTTP_STATUS_OK : HTTP_STATUS_INTERNAL_ERROR;
    }

    if (strcmp(method, "GET") == 0 && strcmp(path, "/api/v1/swap/status") == 0) {
        swap_status_response_t swap_resp;
        int result = enpu_manager_swap_status(mgr, &swap_resp);
        if (result == ENPU_SUCCESS) {
            int offset = 0;
            int fmt_ret = snprintf_s(response, REST_RESPONSE_SIZE, REST_RESPONSE_SIZE - 1,
                                     "{\"buffer\":{\"used\":%lu,\"free\":%lu,\"total\":%lu},\"swapped_models\":[",
                                     swap_resp.buffer_status.used, swap_resp.buffer_status.free,
                                     swap_resp.buffer_status.total);
            CHECK_COND_LOG_(fmt_ret < 0, "snprintf_s swap status failed.");
            offset = (int)strlen(response);

            for (int i = 0; i < swap_resp.swapped_count && offset < REST_RESPONSE_SIZE - 200; i++) {
                if (i > 0) {
                    fmt_ret = snprintf_s(response + offset, REST_RESPONSE_SIZE - offset,
                                         REST_RESPONSE_SIZE - offset - 1, ",");
                    if (fmt_ret < 0) {
                        break;
                    }
                    offset += (int)strlen(response + offset);
                }
                fmt_ret = snprintf_s(response + offset, REST_RESPONSE_SIZE - offset, REST_RESPONSE_SIZE - offset - 1,
                                     "{\"pod_uid\":\"%s\",\"vnpu_id\":%d,\"die_id\":\"%s\",\"swap_size\":%lu}",
                                     swap_resp.swapped_models[i].pod_uid, swap_resp.swapped_models[i].vnpu_id,
                                     swap_resp.swapped_models[i].die_id, swap_resp.swapped_models[i].swap_size);
                if (fmt_ret < 0) {
                    break;
                }
                offset += (int)strlen(response + offset);
            }
            fmt_ret = snprintf_s(response + offset, REST_RESPONSE_SIZE - offset, REST_RESPONSE_SIZE - offset - 1,
                                 "],\"result\":%d}", result);
            CHECK_COND_LOG_(fmt_ret < 0, "snprintf_s swap status failed.");
            return HTTP_STATUS_OK;
        }
        ret = snprintf_s(response, REST_RESPONSE_SIZE, REST_RESPONSE_SIZE - 1,
                         "{\"error\":\"Failed to get swap status\",\"result\":%d}", result);
        CHECK_COND_LOG_(ret < 0, "snprintf_s response failed.");
        return HTTP_STATUS_INTERNAL_ERROR;
    }

    if (strcmp(method, "POST") == 0 && strcmp(path, "/api/v1/swap/clean") == 0) {
        char pod_uid[MAX_UUID_LEN] = "";
        int copy_ret = 0;
        char *p = strstr(body, "\"pod_uid\"");
        if (p) {
            char *v = strchr(p + 9, '"');
            if (v) {
                char *e = strchr(v + 1, '"');
                if (e)
                    copy_ret = strncpy_s(pod_uid, sizeof(pod_uid), v + 1, e - (v + 1));
                CHECK_COND_LOG_(copy_ret != 0, "strncpy_s pod_uid failed.");
            }
        }
        if (pod_uid[0] == '\0') {
            ret = snprintf_s(response, REST_RESPONSE_SIZE, REST_RESPONSE_SIZE - 1,
                             "{\"error\":\"Missing pod_uid\",\"result\":%d}", ENPU_INVALID_PARAM);
            CHECK_COND_LOG_(ret < 0, "snprintf_s response failed.");
            return HTTP_STATUS_BAD_REQUEST;
        }
        int result = enpu_manager_swap_control(mgr, pod_uid, SWAP_ACTION_CLEAN);
        ret = snprintf_s(response, REST_RESPONSE_SIZE, REST_RESPONSE_SIZE - 1, "{\"result\":%d,\"error_msg\":\"\"}",
                         result);
        CHECK_COND_LOG_(ret < 0, "snprintf_s response failed.");
        return result == ENPU_SUCCESS ? HTTP_STATUS_OK : HTTP_STATUS_NOT_FOUND;
    }

    if (strcmp(method, "POST") == 0 && strcmp(path, "/api/v1/swap/force-out") == 0) {
        char pod_uid[MAX_UUID_LEN] = "";
        int copy_ret = 0;
        char *p = strstr(body, "\"pod_uid\"");
        if (p) {
            char *v = strchr(p + 9, '"');
            if (v) {
                char *e = strchr(v + 1, '"');
                if (e)
                    copy_ret = strncpy_s(pod_uid, sizeof(pod_uid), v + 1, e - (v + 1));
                CHECK_COND_LOG_(copy_ret != 0, "strncpy_s pod_uid failed.");
            }
        }
        if (pod_uid[0] == '\0') {
            ret = snprintf_s(response, REST_RESPONSE_SIZE, REST_RESPONSE_SIZE - 1,
                             "{\"error\":\"Missing pod_uid\",\"result\":%d}", ENPU_INVALID_PARAM);
            CHECK_COND_LOG_(ret < 0, "snprintf_s response failed.");
            return HTTP_STATUS_BAD_REQUEST;
        }
        int result = enpu_manager_swap_control(mgr, pod_uid, SWAP_ACTION_FORCE_OUT);
        ret = snprintf_s(response, REST_RESPONSE_SIZE, REST_RESPONSE_SIZE - 1, "{\"result\":%d,\"error_msg\":\"\"}",
                         result);
        CHECK_COND_LOG_(ret < 0, "snprintf_s response failed.");
        return result == ENPU_SUCCESS ? HTTP_STATUS_OK : HTTP_STATUS_NOT_FOUND;
    }

    if (strcmp(method, "POST") == 0 && strcmp(path, "/api/v1/swap/force-in") == 0) {
        char pod_uid[MAX_UUID_LEN] = "";
        int copy_ret = 0;
        char *p = strstr(body, "\"pod_uid\"");
        if (p) {
            char *v = strchr(p + 9, '"');
            if (v) {
                char *e = strchr(v + 1, '"');
                if (e)
                    copy_ret = strncpy_s(pod_uid, sizeof(pod_uid), v + 1, e - (v + 1));
                CHECK_COND_LOG_(copy_ret != 0, "strncpy_s pod_uid failed.");
            }
        }
        if (pod_uid[0] == '\0') {
            ret = snprintf_s(response, REST_RESPONSE_SIZE, REST_RESPONSE_SIZE - 1,
                             "{\"error\":\"Missing pod_uid\",\"result\":%d}", ENPU_INVALID_PARAM);
            CHECK_COND_LOG_(ret < 0, "snprintf_s response failed.");
            return HTTP_STATUS_BAD_REQUEST;
        }
        int result = enpu_manager_swap_control(mgr, pod_uid, SWAP_ACTION_FORCE_IN);
        ret = snprintf_s(response, REST_RESPONSE_SIZE, REST_RESPONSE_SIZE - 1, "{\"result\":%d,\"error_msg\":\"\"}",
                         result);
        CHECK_COND_LOG_(ret < 0, "snprintf_s response failed.");
        return result == ENPU_SUCCESS ? HTTP_STATUS_OK : HTTP_STATUS_NOT_FOUND;
    }

    if (strcmp(method, "DELETE") == 0 && strcmp(path, "/api/v1/clean") == 0) {
        int released = 0;
        int result = enpu_manager_release_all(mgr, &released);
        ret = snprintf_s(response, REST_RESPONSE_SIZE, REST_RESPONSE_SIZE - 1,
                         "{\"released_count\":%d,\"result\":%d,\"error_msg\":\"\"}", released, result);
        CHECK_COND_LOG_(ret < 0, "snprintf_s response failed.");
        return result == ENPU_SUCCESS ? HTTP_STATUS_OK : HTTP_STATUS_INTERNAL_ERROR;
    }

    ret = snprintf_s(response, REST_RESPONSE_SIZE, REST_RESPONSE_SIZE - 1,
                     "{\"error\":\"Unknown endpoint\",\"result\":%d}", ENPU_NOT_FOUND);
    CHECK_COND_LOG_(ret < 0, "snprintf_s response failed.");
    return HTTP_STATUS_NOT_FOUND;
}
static int parse_json_alloc_request(const char *body, alloc_request_t *req)
{
    int ret = memset_s(req, sizeof(alloc_request_t), 0, sizeof(alloc_request_t));
    CHECK_COND_RETURN_ERROR_CODE_LOG(ret != 0, "memset_s req failed.");
    req->predicate_phy_id = -1;
    req->swap_priority = SWAP_PRIORITY_MEDIUM;

    char *p = strstr(body, "\"pod_uid\"");
    if (p) {
        char *v = strchr(p + 9, '"');
        if (v) {
            char *e = strchr(v + 1, '"');
            if (e) {
                int copy_ret = strncpy_s(req->pod_uid, sizeof(req->pod_uid), v + 1, e - (v + 1));
                CHECK_COND_LOG_(copy_ret != 0, "strncpy_s pod_uid failed.");
            }
        }
    }
    p = strstr(body, "\"container_name\"");
    if (p) {
        char *v = strchr(p + 16, '"');
        if (v) {
            char *e = strchr(v + 1, '"');
            if (e) {
                int copy_ret = strncpy_s(req->container_name, sizeof(req->container_name), v + 1, e - (v + 1));
                CHECK_COND_LOG_(copy_ret != 0, "strncpy_s container_name failed.");
            }
        }
    }
    if ((req->pod_uid[0] != '\0' && !is_safe_key(req->pod_uid)) ||
        (req->container_name[0] != '\0' && !is_safe_key(req->container_name))) {
        LOG_ERROR("Invalid pod_uid/container_name: quote, backslash or control char found");
        return -1;
    }

    const char *num = json_number_value(body, "\"aicore_quota\"");
    if (num != NULL) {
        req->aicore_quota = (int)strtol(num, NULL, 10);
    }
    num = json_number_value(body, "\"hbm_request\"");
    if (num != NULL) {
        req->hbm_quota = strtoull(num, NULL, 10);
    } else {
        num = json_number_value(body, "\"hbm_quota\"");
        if (num != NULL) {
            req->hbm_quota = strtoull(num, NULL, 10);
        }
    }
    num = json_number_value(body, "\"hbm_limit\"");
    if (num != NULL) {
        req->hbm_limit = strtoull(num, NULL, 10);
    } else {
        req->hbm_limit = req->hbm_quota;
    }
    num = json_number_value(body, "\"sched_policy\"");
    if (num != NULL) {
        req->sched_policy = (int)strtol(num, NULL, 10);
    }
    num = json_number_value(body, "\"swap_priority\"");
    if (num != NULL) {
        req->swap_priority = (int)strtol(num, NULL, 10);
    }
    num = json_number_value(body, "\"vnpu_priority\"");
    if (num != NULL) {
        req->vnpu_priority = (int)strtol(num, NULL, 10);
    }
    num = json_number_value(body, "\"phy_id\"");
    if (num != NULL) {
        req->predicate_phy_id = (int)strtol(num, NULL, 10);
    }
    return 0;
}
static char *format_devices_json(enpu_manager_t *mgr)
{
    if (!mgr) {
        return strdup("{\"devices\":[],\"error_msg\":\"manager is null\"}");
    }

    device_info_t devices[MAX_NPU_PER_NODE] = {0};
    int count = 0;
    int ret = enpu_manager_query_devices(mgr, NULL, devices, &count);
    if (ret != ENPU_SUCCESS) {
        char err[128];
        int fmt_ret = snprintf_s(err, sizeof(err), sizeof(err) - 1,
                                 "{\"devices\":[],\"error_msg\":\"query failed: ret=%d\"}", ret);
        CHECK_COND_LOG_(fmt_ret < 0, "snprintf_s err failed.");
        return strdup(err);
    }
    if (count == 0) {
        return strdup("{\"devices\":[]}");
    }

    const size_t JSON_BUF_SIZE = 16384;
    char *json = (char *)malloc(JSON_BUF_SIZE);
    if (!json) {
        return strdup("{\"devices\":[],\"error_msg\":\"OOM: failed to alloc json buffer\"}");
    }

    int offset = 0;
    int i = 0;
    int fmt_ret = snprintf_s(json, JSON_BUF_SIZE, JSON_BUF_SIZE - 1, "{\"devices\":[");
    if (fmt_ret < 0) {
        goto overflow;
    }
    offset = (int)strlen(json);

    for (i = 0; i < count; i++) {
        if (i > 0) {
            fmt_ret = snprintf_s(json + offset, JSON_BUF_SIZE - offset, JSON_BUF_SIZE - offset - 1, ",");
            if (fmt_ret < 0) {
                goto overflow;
            }
            offset += (int)strlen(json + offset);
        }

        int aicore = atomic_load(&devices[i].allocatable.aicore_quota);
        uint64_t hbm = atomic_load(&devices[i].allocatable.hbm_quota);

        fmt_ret = snprintf_s(json + offset, JSON_BUF_SIZE - offset, JSON_BUF_SIZE - offset - 1,
                             "{\"phy_id\":%d,\"minor_name\":\"%s\",\"uuid\":\"%s\","
                             "\"aicore_available\":%d,\"hbm_available\":%lu,"
                             "\"vnpu_available\":%d,\"oversub_ratio\":%.2f}",
                             devices[i].meta.phy_id, devices[i].meta.minor_name, devices[i].meta.uuid, aicore, hbm,
                             MAX_VNPU_PER_DIE - devices[i].allocatable.vnpu_count, devices[i].oversub_ratio);
        if (fmt_ret < 0) {
            goto overflow;
        }
        offset += (int)strlen(json + offset);
    }

    fmt_ret = snprintf_s(json + offset, JSON_BUF_SIZE - offset, JSON_BUF_SIZE - offset - 1, "]}");
    CHECK_COND_LOG_(fmt_ret < 0, "snprintf_s devices tail failed.");
    return json;

overflow:
    if (offset >= 0 && (size_t)offset < JSON_BUF_SIZE) {
        fmt_ret =
            snprintf_s(json + offset, JSON_BUF_SIZE - offset, JSON_BUF_SIZE - offset - 1,
                       "],\"error_msg\":\"json buffer overflow at item %d (total %d), only first %d items returned\"",
                       i, count, i);
        CHECK_COND_LOG_(fmt_ret < 0, "snprintf_s overflow msg failed.");
    }
    return json;
}
static char *format_device_json(enpu_manager_t *mgr, int phy_id)
{
    if (!mgr) {
        return strdup("{\"error_msg\":\"manager is null\"}");
    }

    /* 复用 enpu_manager_query_devices 的单卡过滤 */
    device_query_t query = {0};
    query.phy_id = phy_id;

    device_info_t dev;
    int count = 0;
    int ret = enpu_manager_query_devices(mgr, &query, &dev, &count);
    if (ret != ENPU_SUCCESS) {
        char err[128];
        int fmt_ret = snprintf_s(err, sizeof(err), sizeof(err) - 1, "{\"error_msg\":\"query failed: ret=%d\"}", ret);
        CHECK_COND_LOG_(fmt_ret < 0, "snprintf_s err failed.");
        return strdup(err);
    }
    if (count == 0) {
        return NULL; // 404
    }

    int aicore = atomic_load(&dev.allocatable.aicore_quota);
    uint64_t hbm = atomic_load(&dev.allocatable.hbm_quota);

    char *json = (char *)malloc(DEVICE_JSON_BUF_SIZE);
    if (!json) {
        return strdup("{\"error_msg\":\"OOM: failed to alloc json buffer\"}");
    }

    int fmt_ret = snprintf_s(json, DEVICE_JSON_BUF_SIZE, DEVICE_JSON_BUF_SIZE - 1,
                             "{\"phy_id\":%d,\"die_id\":\"%s\","
                             "\"aicore_available\":%d,\"hbm_available\":%lu,"
                             "\"vnpu_available\":%d,\"total_memory\":%lu,"
                             "\"oversub_ratio\":%.2f}",
                             dev.meta.phy_id, dev.meta.die_id, aicore, hbm,
                             MAX_VNPU_PER_DIE - dev.allocatable.vnpu_count, (unsigned long)dev.meta.total_memory,
                             dev.oversub_ratio);
    CHECK_COND_LOG_(fmt_ret < 0, "snprintf_s device json failed.");
    return json;
}
static char *format_allocations_json(enpu_manager_t *mgr)
{
    if (!mgr) {
        return strdup("{\"allocations\":[],\"error_msg\":\"manager is null\"}");
    }

    allocation_t *allocations = (allocation_t *)malloc(sizeof(allocation_t) * MAX_ALLOC_COUNT);
    if (!allocations) {
        return strdup("{\"allocations\":[],\"error_msg\":\"OOM: failed to alloc allocations buffer\"}");
    }

    int count = 0;
    /* 显式构造"不过滤"的 query，npu_allocator 要求 query 非空 */
    allocation_query_t query = {0};
    query.phy_id = -1;

    int ret = enpu_manager_query_allocations(mgr, &query, allocations, &count);
    if (ret != ENPU_SUCCESS) {
        free(allocations);
        char err[128];
        int fmt_ret = snprintf_s(err, sizeof(err), sizeof(err) - 1,
                                 "{\"allocations\":[],\"error_msg\":\"query failed: ret=%d\"}", ret);
        CHECK_COND_LOG_(fmt_ret < 0, "snprintf_s err failed.");
        return strdup(err);
    }
    if (count == 0) {
        free(allocations);
        return strdup("{\"allocations\":[]}");
    }

    if (count > MAX_ALLOC_COUNT) {
        char err[160];
        int fmt_ret = snprintf_s(err, sizeof(err), sizeof(err) - 1,
                                 "{\"allocations\":[],\"error_msg\":\"registry count %d exceeds MAX_ALLOC_COUNT %d "
                                 "(truncated, please report)\"}",
                                 count, MAX_ALLOC_COUNT);
        CHECK_COND_LOG_(fmt_ret < 0, "snprintf_s err failed.");
        free(allocations);
        return strdup(err);
    }

    const size_t JSON_BUF_SIZE = 65535;
    char *json = (char *)malloc(JSON_BUF_SIZE);
    if (!json) {
        free(allocations);
        return strdup("{\"allocations\":[],\"error_msg\":\"OOM: failed to alloc json buffer\"}");
    }

    int offset = 0;
    int i = 0;
    int fmt_ret = snprintf_s(json, JSON_BUF_SIZE, JSON_BUF_SIZE - 1, "{\"allocations\":[");
    if (fmt_ret < 0) {
        goto overflow;
    }
    offset = (int)strlen(json);

    for (i = 0; i < count; i++) {
        allocation_t *alloc = &allocations[i];

        if (i > 0) {
            fmt_ret = snprintf_s(json + offset, JSON_BUF_SIZE - offset, JSON_BUF_SIZE - offset - 1, ",");
            if (fmt_ret < 0) {
                goto overflow;
            }
            offset += (int)strlen(json + offset);
        }

        fmt_ret = snprintf_s(json + offset, JSON_BUF_SIZE - offset, JSON_BUF_SIZE - offset - 1,
                             "{\"pod_uid\":\"%s\",\"container_name\":\"%s\","
                             "\"phy_id\":%d,\"vnpu_id\":%d,"
                             "\"die_id\":\"%s\",\"aicore_quota\":%d,"
                             "\"hbm_quota\":%lu,\"hbm_limit\":%lu,\"sched_policy\":%d,"
                             "\"swap_priority\":%d,\"vnpu_priority\":%d}",
                             alloc->pod_uid, alloc->container_name, alloc->phy_id, alloc->vnpu_id, alloc->die_id,
                             alloc->aicore_quota, alloc->hbm_quota, alloc->hbm_limit, alloc->sched_policy,
                             alloc->swap_priority, alloc->vnpu_priority);
        if (fmt_ret < 0) {
            goto overflow;
        }
        offset += (int)strlen(json + offset);
    }

    fmt_ret = snprintf_s(json + offset, JSON_BUF_SIZE - offset, JSON_BUF_SIZE - offset - 1, "]}");
    CHECK_COND_LOG_(fmt_ret < 0, "snprintf_s allocations tail failed.");
    free(allocations);
    return json;

overflow:
    /* 缓冲区即将溢出：在 JSON 末尾追加 error_msg，告诉用户本次返回不完整 */
    if (offset >= 0 && (size_t)offset < JSON_BUF_SIZE) {
        fmt_ret = snprintf_s(json + offset, JSON_BUF_SIZE - offset, JSON_BUF_SIZE - offset - 1,
                             "],\"error_msg\":\"json buffer (65535B) overflow at item %d (total %d), only first %d "
                             "items returned; raise MAX_ALLOC_COUNT or split the query\"",
                             i, count, i);
        CHECK_COND_LOG_(fmt_ret < 0, "snprintf_s overflow msg failed.");
    }
    free(allocations);
    return json;
}
