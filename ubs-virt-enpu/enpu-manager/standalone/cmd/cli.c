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

#include <arpa/inet.h>
#include <netdb.h>
#include <netinet/in.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>

#include "cli.h"
#include "securec.h"

#define RECV_BUF_SIZE 65536
#define REQ_BUF_SIZE 4096
#define MAX_URL_LEN 256

static char *skip_whitespace(char *p)
{
    while (*p && (*p == ' ' || *p == '\t' || *p == '\n' || *p == '\r')) {
        p++;
    }
    return p;
}

static char *find_value(char *json, const char *key)
{
    char search[128];
    int n = snprintf_s(search, sizeof(search), sizeof(search) - 1, "\"%s\"", key);
    if (n < 0 || n >= (int)sizeof(search)) {
        return NULL;
    }
    char *pos = strstr(json, search);
    if (!pos) {
        return NULL;
    }
    pos += strlen(search);
    pos = skip_whitespace(pos);
    if (*pos != ':') {
        return NULL;
    }
    pos++;
    pos = skip_whitespace(pos);
    return pos;
}

static int parse_int(char *json, const char *key, int *value)
{
    char *pos = find_value(json, key);
    if (!pos) {
        return -1;
    }
    *value = atoi(pos);
    return 0;
}

static int parse_string(char *json, const char *key, char *buf, int buf_len)
{
    char *pos = find_value(json, key);
    if (!pos || *pos != '"') {
        return -1;
    }
    pos++;
    int i = 0;
    while (*pos && *pos != '"' && i < buf_len - 1) {
        buf[i++] = *pos++;
    }
    buf[i] = '\0';
    return 0;
}

static int parse_uint64(char *json, const char *key, uint64_t *value)
{
    char *pos = find_value(json, key);
    if (!pos) {
        return -1;
    }
    *value = strtoull(pos, NULL, 10);
    return 0;
}

static int http_request(const char *host, const char *port, const char *method, const char *path, const char *body,
                        char *response, int resp_len)
{
    int sockfd = -1;
    struct addrinfo hints, *res = NULL, *rp = NULL;
    int ret = -1;

    ret = memset_s(&hints, sizeof(hints), 0, sizeof(hints));
    if (ret != 0) {
        fprintf(stderr, "Error: Failed to memset hints\n");
        return ret;
    }
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;

    if (getaddrinfo(host, port, &hints, &res) != 0) {
        fprintf(stderr, "Error: Failed to resolve host %s\n", host);
        return -1;
    }

    for (rp = res; rp != NULL; rp = rp->ai_next) {
        sockfd = socket(rp->ai_family, rp->ai_socktype, rp->ai_protocol);
        if (sockfd == -1) {
            continue;
        }
        if (connect(sockfd, rp->ai_addr, rp->ai_addrlen) == 0) {
            break;
        }
        close(sockfd);
        sockfd = -1;
    }
    freeaddrinfo(res);

    if (sockfd == -1) {
        fprintf(stderr, "Error: Failed to connect to %s:%s\n", host, port);
        return -1;
    }

    char request[REQ_BUF_SIZE];
    int req_len;
    if (body && strlen(body) > 0) {
        req_len = snprintf_s(request, sizeof(request), sizeof(request) - 1,
                             "%s %s HTTP/1.1\r\n"
                             "Host: %s:%s\r\n"
                             "Content-Type: application/json\r\n"
                             "Content-Length: %zu\r\n"
                             "Connection: close\r\n"
                             "\r\n"
                             "%s",
                             method, path, host, port, strlen(body), body);
    } else {
        req_len = snprintf_s(request, sizeof(request), sizeof(request) - 1,
                             "%s %s HTTP/1.1\r\n"
                             "Host: %s:%s\r\n"
                             "Connection: close\r\n"
                             "\r\n",
                             method, path, host, port);
    }

    if (req_len < 0 || req_len >= (int)sizeof(request)) {
        fprintf(stderr, "Error: Failed to build request (len=%d)\n", req_len);
        close(sockfd);
        return -1;
    }

    if (send(sockfd, request, req_len, 0) < 0) {
        fprintf(stderr, "Error: Failed to send request\n");
        close(sockfd);
        return -1;
    }

    int total = 0;
    int n;
    while ((n = recv(sockfd, response + total, resp_len - total - 1, 0)) > 0) {
        total += n;
        if (total >= resp_len - 1) {
            break;
        }
    }
    response[total] = '\0';
    close(sockfd);

    return total;
}

static char *get_body(char *response)
{
    char *body = strstr(response, "\r\n\r\n");
    if (body) {
        return body + 4;
    }
    return response;
}

static int get_status_code(char *response)
{
    int code = 0;
    int ret = sscanf_s(response, "HTTP/1.%*d %d", &code);
    if (ret == 1) {
        return code;
    }
    return -1;
}

static void print_usage(const char *prog)
{
    printf("Usage: %s <command> [options]\n\n", prog);
    printf("Commands:\n");
    printf("  list                     List all NPU devices\n");
    printf("  alloc -n <aicore> [-r <request>] [-l <limit>] | -m <hbm> -p <policy> "
           "[-d <phy_id>] [-u <pod_uid>] [-c <container>]\n");
    printf("                           Allocate vNPU\n");
    printf("                           -r/--request: hbm request (保底预留)\n");
    printf("                           -l/--limit:   hbm limit (硬边界上限)\n");
    printf("                           -m:           hbm quota (向后兼容，等同于 -r -l 相同)\n");
    printf("                           -d:           specify physical card ID (>=0 直接分配到该卡；-1 自动选择)\n");
    printf("                           -u:           pod_uid (可选；未指定时默认 pod-<phy>)\n");
    printf("                           -c:           container_name (可选；未指定时默认 container-<phy>-<vnpu>)\n");
    printf("  release -u <pod_uid> -c <container>\n");
    printf("                           Release vNPU\n");
    printf("  status                   Show allocation status\n");
    printf("  checkpoint               Trigger checkpoint\n");
    printf("  recover                  Trigger recovery\n");
    printf("  swap status              Show swap buffer status\n");
    printf("  swap clean <pod_uid>     Clean swap data for pod\n");
    printf("  swap out <pod_uid>       Force swap out\n");
    printf("  swap in <pod_uid>        Force swap in\n");
    printf("\nOptions:\n");
    printf("  --server <host:port>     Server address (default: localhost:8080)\n");
}

static int cmd_list(int argc, char *argv[], const char *host, const char *port)
{
    char response[RECV_BUF_SIZE];
    int ret = http_request(host, port, "GET", "/api/v1/devices", NULL, response, sizeof(response));
    if (ret < 0) {
        return 1;
    }

    int status = get_status_code(response);
    if (status != 200) {
        fprintf(stderr, "Error: HTTP %d\n", status);
        return 1;
    }

    char *body = get_body(response);
    printf("NPU Devices:\n%s\n", body);
    return 0;
}

static int cmd_alloc(int argc, char *argv[], const char *host, const char *port)
{
    int aicore = -1;
    uint64_t hbm = 0;
    uint64_t hbm_request = 0;
    uint64_t hbm_limit = 0;
    int policy = 0;
    int phy_id = -1;
    char pod_uid[128] = "";
    char container[64] = "";

    int i = 2;
    while (i < argc) {
        const char *val = (i + 1 < argc) ? argv[i + 1] : NULL;
        int has_val = (val != NULL);

        if (strcmp(argv[i], "-n") == 0 && has_val) {
            aicore = atoi(val);
        } else if (strcmp(argv[i], "-m") == 0 && has_val) {
            hbm = strtoull(val, NULL, 10);
        } else if (strcmp(argv[i], "-r") == 0 && has_val) {
            hbm_request = strtoull(val, NULL, 10);
        } else if (strcmp(argv[i], "-l") == 0 && has_val) {
            hbm_limit = strtoull(val, NULL, 10);
        } else if (strcmp(argv[i], "--request") == 0 && has_val) {
            hbm_request = strtoull(val, NULL, 10);
        } else if (strcmp(argv[i], "--limit") == 0 && has_val) {
            hbm_limit = strtoull(val, NULL, 10);
        } else if (strcmp(argv[i], "-p") == 0 && has_val) {
            if (strcmp(val, "fixed-share") == 0) {
                policy = 1;
            } else if (strcmp(val, "elastic") == 0) {
                policy = 2;
            } else if (strcmp(val, "best-effort") == 0) {
                policy = 3;
            } else {
                policy = atoi(val);
            }
        } else if (strcmp(argv[i], "-d") == 0 && has_val) {
            phy_id = atoi(val);
        } else if (strcmp(argv[i], "-u") == 0 && has_val) {
            if (strncpy_s(pod_uid, sizeof(pod_uid), val, strnlen(val, sizeof(pod_uid) - 1)) != 0) {
                fprintf(stderr, "Error: Failed to copy pod_uid\n");
                return 1;
            }
        } else if (strcmp(argv[i], "-c") == 0 && has_val) {
            if (strncpy_s(container, sizeof(container), val, strnlen(val, sizeof(container) - 1)) != 0) {
                fprintf(stderr, "Error: Failed to copy container\n");
                return 1;
            }
        } else {
            has_val = 0;
        }

        i += has_val ? 2 : 1;
    }

    if (aicore < 0 || policy == 0) {
        fprintf(stderr, "Error: -n <aicore> and -p <policy> are required\n");
        return 1;
    }

    if (hbm_request == 0 && hbm_limit == 0 && hbm == 0) {
        fprintf(stderr, "Error: -r/--request and -l/--limit or -m <hbm> are required\n");
        return 1;
    }

    if (hbm_request > 0 && hbm_limit > 0) {
        char body[512];
        int body_len = snprintf_s(body, sizeof(body), sizeof(body) - 1,
                                  "{\"aicore_quota\":%d,\"hbm_request\":%lu,\"hbm_limit\":%lu,"
                                  "\"sched_policy\":%d,\"phy_id\":%d,\"pod_uid\":\"%s\","
                                  "\"container_name\":\"%s\"}",
                                  aicore, hbm_request, hbm_limit, policy, phy_id, pod_uid, container);
        if (body_len < 0 || body_len >= (int)sizeof(body)) {
            fprintf(stderr, "Error: Failed to build request body\n");
            return 1;
        }

        char response[RECV_BUF_SIZE];
        int ret = http_request(host, port, "POST", "/api/v1/allocate", body, response, sizeof(response));
        if (ret < 0) {
            return 1;
        }

        int status = get_status_code(response);
        char *resp_body = get_body(response);

        if (status != 200 && status != 201) {
            fprintf(stderr, "Error: HTTP %d - %s\n", status, resp_body);
            return 1;
        }

        int resp_phy_id = 0, resp_vnpu_id = 0, resp_aicore = 0, resp_sched_policy = 0;
        uint64_t resp_hbm_request = 0, resp_hbm_limit = 0;
        char shm_id[128] = "", minor_name[64] = "";
        char resp_pod_uid[128] = "", resp_container[128] = "";
        char resp_die_id[64] = "";

        parse_int(resp_body, "phy_id", &resp_phy_id);
        parse_int(resp_body, "vnpu_id", &resp_vnpu_id);
        parse_string(resp_body, "pod_uid", resp_pod_uid, sizeof(resp_pod_uid));
        parse_string(resp_body, "container_name", resp_container, sizeof(resp_container));
        parse_string(resp_body, "die_id", resp_die_id, sizeof(resp_die_id));
        parse_string(resp_body, "shm_id", shm_id, sizeof(shm_id));
        parse_int(resp_body, "aicore_quota", &resp_aicore);
        parse_int(resp_body, "sched_policy", &resp_sched_policy);
        parse_uint64(resp_body, "hbm_quota", &resp_hbm_request);
        parse_uint64(resp_body, "hbm_limit", &resp_hbm_limit);
        parse_string(resp_body, "minor_name", minor_name, sizeof(minor_name));

        printf("vNPU Allocated:\n");
        printf("  Physical ID:  %d\n", resp_phy_id);
        printf("  Virtual ID:   %d\n", resp_vnpu_id);
        printf("  Pod UID:      %s\n", resp_pod_uid);
        printf("  Container:    %s\n", resp_container);
        printf("  Die ID:       %s\n", resp_die_id);
        printf("  AI Core:      %d\n", resp_aicore);
        printf("  HBM Request:  %lu\n", resp_hbm_request);
        printf("  HBM Limit:    %lu\n", resp_hbm_limit);
        printf("  Policy:       %d\n", resp_sched_policy);
        printf("  SHM ID:       %s\n", shm_id);
        printf("  Minor:        %s\n", minor_name);
        return 0;
    }

    if (hbm > 0) {
        hbm_request = hbm;
        hbm_limit = hbm;
    }

    char body[512];
    int body_len = snprintf_s(body, sizeof(body), sizeof(body) - 1,
                              "{\"aicore_quota\":%d,\"hbm_quota\":%lu,\"sched_policy\":%d,"
                              "\"phy_id\":%d,\"pod_uid\":\"%s\",\"container_name\":\"%s\"}",
                              aicore, hbm_request, policy, phy_id, pod_uid, container);
    if (body_len < 0 || body_len >= (int)sizeof(body)) {
        fprintf(stderr, "Error: Failed to build request body\n");
        return 1;
    }

    char response[RECV_BUF_SIZE];
    int ret = http_request(host, port, "POST", "/api/v1/allocate", body, response, sizeof(response));
    if (ret < 0) {
        return 1;
    }

    int status = get_status_code(response);
    char *resp_body = get_body(response);

    if (status != 200 && status != 201) {
        fprintf(stderr, "Error: HTTP %d - %s\n", status, resp_body);
        return 1;
    }

    int phy_id_resp = 0, vnpu_id = 0, aicore_quota = 0, sched_policy = 0;
    char shm_id[128] = "", minor_name[64] = "";
    char resp_pod_uid[128] = "", resp_container[128] = "";
    char die_id[64] = "";

    parse_int(resp_body, "phy_id", &phy_id_resp);
    parse_int(resp_body, "vnpu_id", &vnpu_id);
    parse_string(resp_body, "pod_uid", resp_pod_uid, sizeof(resp_pod_uid));
    parse_string(resp_body, "container_name", resp_container, sizeof(resp_container));
    parse_string(resp_body, "die_id", die_id, sizeof(die_id));
    parse_string(resp_body, "shm_id", shm_id, sizeof(shm_id));
    parse_int(resp_body, "aicore_quota", &aicore_quota);
    parse_int(resp_body, "sched_policy", &sched_policy);
    parse_string(resp_body, "minor_name", minor_name, sizeof(minor_name));

    printf("vNPU Allocated:\n");
    printf("  Physical ID:  %d\n", phy_id_resp);
    printf("  Virtual ID:   %d\n", vnpu_id);
    printf("  Pod UID:      %s\n", resp_pod_uid);
    printf("  Container:    %s\n", resp_container);
    printf("  Die ID:       %s\n", die_id);
    printf("  AI Core:      %d\n", aicore_quota);
    printf("  Policy:       %d\n", sched_policy);
    printf("  SHM ID:       %s\n", shm_id);
    printf("  Minor:        %s\n", minor_name);
    return 0;
}

static int cmd_release(int argc, char *argv[], const char *host, const char *port)
{
    char pod_uid[128] = "";
    char container[64] = "";

    int i = 2;
    while (i < argc) {
        const char *val = (i + 1 < argc) ? argv[i + 1] : NULL;
        int has_val = (val != NULL);

        if (strcmp(argv[i], "-u") == 0 && has_val) {
            if (strncpy_s(pod_uid, sizeof(pod_uid), val, strnlen(val, sizeof(pod_uid) - 1)) != 0) {
                fprintf(stderr, "Error: Failed to copy pod_uid\n");
                return 1;
            }
        } else if (strcmp(argv[i], "-c") == 0 && has_val) {
            if (strncpy_s(container, sizeof(container), val, strnlen(val, sizeof(container) - 1)) != 0) {
                fprintf(stderr, "Error: Failed to copy container\n");
                return 1;
            }
        } else {
            has_val = 0;
        }

        i += has_val ? 2 : 1;
    }

    if (strlen(pod_uid) == 0 || strlen(container) == 0) {
        fprintf(stderr, "Error: -u <pod_uid> and -c <container> are required\n");
        return 1;
    }

    char path[MAX_URL_LEN];
    int path_len = snprintf_s(path, sizeof(path), sizeof(path) - 1, "/api/v1/allocate/%s/%s", pod_uid, container);
    if (path_len < 0 || path_len >= (int)sizeof(path)) {
        fprintf(stderr, "Error: Failed to build request path\n");
        return 1;
    }

    char response[RECV_BUF_SIZE];
    int ret = http_request(host, port, "DELETE", path, NULL, response, sizeof(response));
    if (ret < 0) {
        return 1;
    }

    int status = get_status_code(response);
    if (status != 200) {
        fprintf(stderr, "Error: HTTP %d - %s\n", status, get_body(response));
        return 1;
    }

    printf("vNPU Released successfully\n");
    return 0;
}

static int cmd_status(int argc, char *argv[], const char *host, const char *port)
{
    char response[RECV_BUF_SIZE];
    int ret = http_request(host, port, "GET", "/api/v1/allocations", NULL, response, sizeof(response));
    if (ret < 0) {
        return 1;
    }

    int status = get_status_code(response);
    if (status != 200) {
        fprintf(stderr, "Error: HTTP %d\n", status);
        return 1;
    }

    char *body = get_body(response);
    printf("Allocation Status:\n%s\n", body);
    return 0;
}

static int cmd_checkpoint(int argc, char *argv[], const char *host, const char *port)
{
    char response[RECV_BUF_SIZE];
    int ret = http_request(host, port, "POST", "/api/v1/checkpoint", "{}", response, sizeof(response));
    if (ret < 0) {
        return 1;
    }

    int status = get_status_code(response);
    if (status != 200 && status != 201) {
        fprintf(stderr, "Error: HTTP %d - %s\n", status, get_body(response));
        return 1;
    }

    printf("Checkpoint created successfully\n");
    return 0;
}

static int cmd_recover(int argc, char *argv[], const char *host, const char *port)
{
    char response[RECV_BUF_SIZE];
    int ret = http_request(host, port, "POST", "/api/v1/recover", "{}", response, sizeof(response));
    if (ret < 0) {
        return 1;
    }

    int status = get_status_code(response);
    if (status != 200 && status != 201) {
        fprintf(stderr, "Error: HTTP %d - %s\n", status, get_body(response));
        return 1;
    }

    printf("Recovery completed successfully\n");
    return 0;
}

static int cmd_swap_status(int argc, char *argv[], const char *host, const char *port)
{
    char response[RECV_BUF_SIZE];
    int ret = http_request(host, port, "GET", "/api/v1/swap/status", NULL, response, sizeof(response));
    if (ret < 0) {
        return 1;
    }

    int status = get_status_code(response);
    if (status != 200) {
        fprintf(stderr, "Error: HTTP %d - %s\n", status, get_body(response));
        return 1;
    }

    char *body = get_body(response);

    uint64_t used = 0, free_space = 0, total = 0;
    parse_uint64(body, "used", &used);
    parse_uint64(body, "free", &free_space);
    parse_uint64(body, "total", &total);

    printf("Swap Buffer Status:\n");
    printf("  Used:  %lu bytes\n", used);
    printf("  Free:  %lu bytes\n", free_space);
    printf("  Total: %lu bytes\n", total);
    printf("\nSwapped Models:\n%s\n", body);
    return 0;
}

static int cmd_swap_clean(int argc, char *argv[], const char *host, const char *port)
{
    if (argc < 4) {
        fprintf(stderr, "Error: pod_uid required\n");
        return 1;
    }

    char pod_uid[128] = "";
    if (strncpy_s(pod_uid, sizeof(pod_uid), argv[3], strnlen(argv[3], sizeof(pod_uid) - 1)) != 0) {
        fprintf(stderr, "Error: Failed to copy pod_uid\n");
        return 1;
    }

    char body[256];
    int body_len = snprintf_s(body, sizeof(body), sizeof(body) - 1, "{\"pod_uid\":\"%s\"}", pod_uid);
    if (body_len < 0 || body_len >= (int)sizeof(body)) {
        fprintf(stderr, "Error: Failed to build request body\n");
        return 1;
    }

    char response[RECV_BUF_SIZE];
    int ret = http_request(host, port, "POST", "/api/v1/swap/clean", body, response, sizeof(response));
    if (ret < 0) {
        return 1;
    }

    int status = get_status_code(response);
    if (status != 200) {
        fprintf(stderr, "Error: HTTP %d - %s\n", status, get_body(response));
        return 1;
    }

    printf("Swap data cleaned for pod: %s\n", pod_uid);
    return 0;
}

static int cmd_swap_out(int argc, char *argv[], const char *host, const char *port)
{
    if (argc < 4) {
        fprintf(stderr, "Error: pod_uid required\n");
        return 1;
    }

    char pod_uid[128] = "";
    if (strncpy_s(pod_uid, sizeof(pod_uid), argv[3], strnlen(argv[3], sizeof(pod_uid) - 1)) != 0) {
        fprintf(stderr, "Error: Failed to copy pod_uid\n");
        return 1;
    }

    char body[256];
    int body_len = snprintf_s(body, sizeof(body), sizeof(body) - 1, "{\"pod_uid\":\"%s\"}", pod_uid);
    if (body_len < 0 || body_len >= (int)sizeof(body)) {
        fprintf(stderr, "Error: Failed to build request body\n");
        return 1;
    }

    char response[RECV_BUF_SIZE];
    int ret = http_request(host, port, "POST", "/api/v1/swap/force-out", body, response, sizeof(response));
    if (ret < 0) {
        return 1;
    }

    int status = get_status_code(response);
    if (status != 200) {
        fprintf(stderr, "Error: HTTP %d - %s\n", status, get_body(response));
        return 1;
    }

    printf("Force swap out triggered for pod: %s\n", pod_uid);
    return 0;
}

static int cmd_swap_in(int argc, char *argv[], const char *host, const char *port)
{
    if (argc < 4) {
        fprintf(stderr, "Error: pod_uid required\n");
        return 1;
    }

    char pod_uid[128] = "";
    if (strncpy_s(pod_uid, sizeof(pod_uid), argv[3], strnlen(argv[3], sizeof(pod_uid) - 1)) != 0) {
        fprintf(stderr, "Error: Failed to copy pod_uid\n");
        return 1;
    }

    char body[256];
    int body_len = snprintf_s(body, sizeof(body), sizeof(body) - 1, "{\"pod_uid\":\"%s\"}", pod_uid);
    if (body_len < 0 || body_len >= (int)sizeof(body)) {
        fprintf(stderr, "Error: Failed to build request body\n");
        return 1;
    }

    char response[RECV_BUF_SIZE];
    int ret = http_request(host, port, "POST", "/api/v1/swap/force-in", body, response, sizeof(response));
    if (ret < 0) {
        return 1;
    }

    int status = get_status_code(response);
    if (status != 200) {
        fprintf(stderr, "Error: HTTP %d - %s\n", status, get_body(response));
        return 1;
    }

    printf("Force swap in triggered for pod: %s\n", pod_uid);
    return 0;
}

int cli_main(int argc, char *argv[])
{
    if (argc < 2) {
        print_usage(argv[0]);
        return 1;
    }

    const char *host = CLI_DEFAULT_HOST;
    const char *port = CLI_DEFAULT_PORT;

    int i = 1;
    while (i < argc) {
        if (strcmp(argv[i], "--server") == 0 && i + 1 < argc) {
            char *server = argv[i + 1];
            char *colon = strchr(server, ':');
            if (colon) {
                *colon = '\0';
                host = server;
                port = colon + 1;
            } else {
                host = server;
            }
            i += 2;
        } else {
            i++;
        }
    }

    const char *cmd = argv[1];

    if (strcmp(cmd, "list") == 0) {
        return cmd_list(argc, argv, host, port);
    } else if (strcmp(cmd, "alloc") == 0) {
        return cmd_alloc(argc, argv, host, port);
    } else if (strcmp(cmd, "release") == 0) {
        return cmd_release(argc, argv, host, port);
    } else if (strcmp(cmd, "status") == 0) {
        return cmd_status(argc, argv, host, port);
    } else if (strcmp(cmd, "checkpoint") == 0) {
        return cmd_checkpoint(argc, argv, host, port);
    } else if (strcmp(cmd, "recover") == 0) {
        return cmd_recover(argc, argv, host, port);
    } else if (strcmp(cmd, "swap") == 0) {
        if (argc < 3) {
            fprintf(stderr, "Error: swap subcommand required (status|clean|out|in)\n");
            return 1;
        }
        const char *subcmd = argv[2];
        if (strcmp(subcmd, "status") == 0) {
            return cmd_swap_status(argc, argv, host, port);
        } else if (strcmp(subcmd, "clean") == 0) {
            return cmd_swap_clean(argc, argv, host, port);
        } else if (strcmp(subcmd, "out") == 0) {
            return cmd_swap_out(argc, argv, host, port);
        } else if (strcmp(subcmd, "in") == 0) {
            return cmd_swap_in(argc, argv, host, port);
        } else {
            fprintf(stderr, "Error: Unknown swap subcommand '%s'\n", subcmd);
            return 1;
        }
    } else if (strcmp(cmd, "-h") == 0 || strcmp(cmd, "--help") == 0) {
        print_usage(argv[0]);
        return 0;
    } else {
        fprintf(stderr, "Error: Unknown command '%s'\n", cmd);
        print_usage(argv[0]);
        return 1;
    }
}

int main(int argc, char *argv[])
{
    return cli_main(argc, argv);
}