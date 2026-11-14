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

#include <errno.h>
#include <limits.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>

#include "allocation_id.h"
#include "cli.h"
#include "securec.h"

#define RECV_BUF_SIZE 65536
#define REQ_BUF_SIZE 4096
#define MAX_URL_LEN 1024

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

static int http_request(const char *socket_path, const char *method, const char *path, const char *body, char *response,
                        int resp_len)
{
    int sockfd = -1;
    if (socket_path == NULL || socket_path[0] != '/' ||
        strnlen(socket_path, sizeof(((struct sockaddr_un *)0)->sun_path)) >=
            sizeof(((struct sockaddr_un *)0)->sun_path)) {
        fprintf(stderr, "Error: Invalid Unix socket path\n");
        return -1;
    }
    struct sockaddr_un addr;
    int ret = memset_s(&addr, sizeof(addr), 0, sizeof(addr));
    if (ret != 0) {
        fprintf(stderr, "Error: Failed to initialize Unix socket address\n");
        return -1;
    }
    addr.sun_family = AF_UNIX;
    ret = strncpy_s(addr.sun_path, sizeof(addr.sun_path), socket_path, sizeof(addr.sun_path) - 1);
    if (ret != 0) {
        fprintf(stderr, "Error: Failed to copy Unix socket path\n");
        return -1;
    }
    sockfd = socket(AF_UNIX, SOCK_STREAM, 0);
    if (sockfd < 0 || connect(sockfd, (struct sockaddr *)&addr, sizeof(addr)) != 0) {
        fprintf(stderr, "Error: Failed to connect to Unix socket %s: %s\n", socket_path, strerror(errno));
        if (sockfd >= 0) {
            close(sockfd);
        }
        return -1;
    }

    char request[REQ_BUF_SIZE];
    int req_len;
    if (body && strlen(body) > 0) {
        req_len = snprintf_s(request, sizeof(request), sizeof(request) - 1,
                             "%s %s HTTP/1.1\r\n"
                             "Host: localhost\r\n"
                             "Content-Type: application/json\r\n"
                             "Content-Length: %zu\r\n"
                             "Connection: close\r\n"
                             "\r\n"
                             "%s",
                             method, path, strlen(body), body);
    } else {
        req_len = snprintf_s(request, sizeof(request), sizeof(request) - 1,
                             "%s %s HTTP/1.1\r\n"
                             "Host: localhost\r\n"
                             "Connection: close\r\n"
                             "\r\n",
                             method, path);
    }

    if (req_len < 0 || req_len >= (int)sizeof(request)) {
        fprintf(stderr, "Error: Failed to build request (len=%d)\n", req_len);
        close(sockfd);
        return -1;
    }

    for (int sent = 0; sent < req_len;) {
        ssize_t n = send(sockfd, request + sent, (size_t)(req_len - sent), MSG_NOSIGNAL);
        if (n < 0 && errno == EINTR) {
            continue;
        }
        if (n <= 0) {
            fprintf(stderr, "Error: Failed to send request\n");
            close(sockfd);
            return -1;
        }
        sent += (int)n;
    }

    int total = 0;
    int n;
    while (total < resp_len - 1) {
        n = recv(sockfd, response + total, (size_t)(resp_len - total - 1), 0);
        if (n < 0 && errno == EINTR) {
            continue;
        }
        if (n < 0) {
            fprintf(stderr, "Error: Failed to receive complete response\n");
            close(sockfd);
            return -1;
        }
        if (n == 0) {
            break;
        }
        total += n;
    }
    if (total == resp_len - 1) {
        fprintf(stderr, "Error: HTTP response exceeds client capacity\n");
        close(sockfd);
        return -1;
    }
    response[total] = '\0';
    close(sockfd);

    char *header_end = strstr(response, "\r\n\r\n");
    char *length_header = strstr(response, "\r\nContent-Length:");
    if (header_end == NULL) {
        return -1;
    }
    if (length_header != NULL && length_header < header_end) {
        char *end = NULL;
        errno = 0;
        unsigned long long expected = strtoull(length_header + strlen("\r\nContent-Length:"), &end, 10);
        if (errno || end == NULL || strncmp(end, "\r\n", 2) != 0 ||
            expected != (unsigned long long)(response + total - header_end - 4)) {
            fprintf(stderr, "Error: Incomplete or invalid HTTP response length\n");
            return -1;
        }
    }

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
    printf("  alloc [--allocation-id <allocation_id>] -n <aicore> [-r <request>] [-l <limit>] | -m <hbm> -p <policy> "
           "[-d <phy_id>] [--swap-order 0|1|2] [-u <pod_uid>] [-c <container>]\n");
    printf("                           Allocate vNPU\n");
    printf("                           -r/--request: hbm request (保底预留)\n");
    printf("                           -l/--limit:   hbm limit (硬边界上限)\n");
    printf("                           -m:           hbm quota (向后兼容，等同于 -r -l 相同)\n");
    printf("                           -d:           specify physical card ID (>=0 直接分配到该卡；-1 自动选择)\n");
    printf("                           -u:           pod_uid (optional metadata)\n");
    printf("                           -c:           container_name (optional metadata)\n");
    printf("                           --swap-order: 0 first, 1 default, 2 last\n");
    printf("  release -a <allocation_id> | -u <pod_uid> -c <container>\n");
    printf("                           Release vNPU\n");
    printf("  status                   Show allocation status\n");
    printf("  resources [--allocation-id UUID | --phy-id N [--vnpu-id N] | "
           "[--pod-uid UID] [--container-name NAME]] [--detail]\n");
    printf("                           Show complete resource JSON\n");
    printf("                           --detail: include stats and calculation diagnostics\n");
    printf("  checkpoint               Trigger checkpoint\n");
    printf("  recover                  Trigger recovery\n");
    printf("  swap status              Show swap buffer status\n");
    printf("  swap clean <pod_uid>     Clean swap data for pod\n");
    printf("  swap out <pod_uid>       Force swap out\n");
    printf("  swap in <pod_uid>        Force swap in\n");
    printf("\nOptions:\n");
    printf("  --socket <path>          Unix socket path (default: %s)\n", CLI_DEFAULT_SOCKET_PATH);
}

static int cmd_list(int argc, char *argv[], const char *socket_path)
{
    char response[RECV_BUF_SIZE];
    int ret = http_request(socket_path, "GET", "/api/v1/devices", NULL, response, sizeof(response));
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

static int resource_cli_id(const char *value, int *id)
{
    if (*value == '\0') {
        return -1;
    }
    for (const char *p = value; *p; ++p) {
        if (*p < '0' || *p > '9') {
            return -1;
        }
    }
    char *end = NULL;
    errno = 0;
    long n = strtol(value, &end, 10);
    if (errno || *end || n > INT_MAX) {
        return -1;
    }
    *id = (int)n;
    return 0;
}

static int resource_url_encode(const char *value, char *output, size_t capacity)
{
    static const char hex[] = "0123456789ABCDEF";
    size_t used = 0;
    if (value == NULL || *value == '\0') {
        return -1;
    }
    for (const unsigned char *p = (const unsigned char *)value; *p != '\0'; ++p) {
        bool plain = (*p >= 'a' && *p <= 'z') || (*p >= 'A' && *p <= 'Z') || (*p >= '0' && *p <= '9') || *p == '-' ||
                     *p == '_' || *p == '.' || *p == '~';
        size_t needed = plain ? 1 : 3;
        if (*p < 0x20 || used + needed >= capacity) {
            return -1;
        }
        if (plain) {
            output[used++] = (char)*p;
        } else {
            output[used++] = '%';
            output[used++] = hex[*p >> 4];
            output[used++] = hex[*p & 0x0f];
        }
    }
    output[used] = '\0';
    return 0;
}

/* Pretty-print without parsing numbers through double (HBM counters are uint64).
 * String escape sequences are copied unchanged, including escaped quotes. */
static void print_resource_json(const char *body)
{
    int indent = 0;
    int quoted = 0, escaped = 0;
    for (const char *p = body; *p; ++p) {
        char c = *p;
        if (quoted) {
            putchar(c);
            if (escaped) {
                escaped = 0;
            } else if (c == '\\') {
                escaped = 1;
            } else if (c == '"') {
                quoted = 0;
            }
            continue;
        }
        if (c == ' ' || c == '\n' || c == '\r' || c == '\t') {
            continue;
        }
        if (c == '"') {
            quoted = 1;
            putchar(c);
        } else if (c == '{' || c == '[') {
            putchar(c);
            putchar('\n');
            indent++;
            printf("%*s", indent * 2, "");
        } else if (c == '}' || c == ']') {
            indent--;
            putchar('\n');
            printf("%*s%c", indent * 2, "", c);
        } else if (c == ',') {
            putchar(c);
            putchar('\n');
            printf("%*s", indent * 2, "");
        } else if (c == ':') {
            printf(": ");
        } else {
            putchar(c);
        }
    }
    putchar('\n');
}

static int cmd_resources(int argc, char *argv[], const char *socket_path)
{
    int phy = -1, vnpu = -1, detail = 0;
    char allocation_id[128] = {0};
    char pod_uid[384] = {0};
    char container_name[192] = {0};
    for (int i = 2; i < argc; ++i) {
        if (strcmp(argv[i], "--socket") == 0 && i + 1 < argc) {
            ++i;
            continue;
        }
        if (strcmp(argv[i], "--detail") == 0 && !detail) {
            detail = 1;
            continue;
        }
        char *text = strcmp(argv[i], "--allocation-id") == 0  ? allocation_id :
                     strcmp(argv[i], "--pod-uid") == 0        ? pod_uid :
                     strcmp(argv[i], "--container-name") == 0 ? container_name :
                                                                NULL;
        size_t text_capacity = text == allocation_id ? sizeof(allocation_id) :
                               text == pod_uid       ? sizeof(pod_uid) :
                                                       sizeof(container_name);
        if (text != NULL) {
            if (*text != '\0' || i + 1 >= argc || resource_url_encode(argv[++i], text, text_capacity) != 0) {
                fprintf(stderr, "Error: Invalid or duplicate resources option\n");
                return 1;
            }
            continue;
        }
        int *id = strcmp(argv[i], "--phy-id") == 0 ? &phy : strcmp(argv[i], "--vnpu-id") == 0 ? &vnpu : NULL;
        if (id == NULL || *id != -1 || i + 1 >= argc || resource_cli_id(argv[++i], id) != 0) {
            fprintf(stderr, "Error: Invalid or duplicate resources option\n");
            return 1;
        }
    }
    if (vnpu >= 0 && phy < 0) {
        fprintf(stderr, "Error: --vnpu-id requires --phy-id\n");
        return 1;
    }
    int selector_count = allocation_id[0] != '\0';
    selector_count += phy >= 0;
    selector_count += pod_uid[0] != '\0' || container_name[0] != '\0';
    if (selector_count > 1) {
        fprintf(stderr, "Error: allocation, resource and metadata selectors are mutually exclusive\n");
        return 1;
    }
    char path[MAX_URL_LEN];
    int n;
    if (allocation_id[0] != '\0') {
        n = snprintf_s(path, sizeof(path), sizeof(path) - 1, "/api/v1/resources?allocation_id=%s&detail=%s",
                       allocation_id, detail ? "true" : "false");
    } else if (pod_uid[0] != '\0' && container_name[0] != '\0') {
        n = snprintf_s(path, sizeof(path), sizeof(path) - 1, "/api/v1/resources?pod_uid=%s&container_name=%s&detail=%s",
                       pod_uid, container_name, detail ? "true" : "false");
    } else if (pod_uid[0] != '\0' || container_name[0] != '\0') {
        n = snprintf_s(path, sizeof(path), sizeof(path) - 1, "/api/v1/resources?%s=%s&detail=%s",
                       pod_uid[0] != '\0' ? "pod_uid" : "container_name", pod_uid[0] != '\0' ? pod_uid : container_name,
                       detail ? "true" : "false");
    } else if (vnpu >= 0) {
        n = snprintf_s(path, sizeof(path), sizeof(path) - 1, "/api/v1/resources?phy_id=%d&vnpu_id=%d&detail=%s", phy,
                       vnpu, detail ? "true" : "false");
    } else if (phy >= 0) {
        n = snprintf_s(path, sizeof(path), sizeof(path) - 1, "/api/v1/resources?phy_id=%d&detail=%s", phy,
                       detail ? "true" : "false");
    } else {
        n = snprintf_s(path, sizeof(path), sizeof(path) - 1, "/api/v1/resources?detail=%s", detail ? "true" : "false");
    }
    if (n < 0) {
        return 1;
    }
    /* Match the server body limit plus room for headers; never silently truncate. */
    const int capacity = 8 * 1024 * 1024 + 4096;
    char *response = malloc((size_t)capacity);
    if (response == NULL) {
        fprintf(stderr, "Error: Out of memory\n");
        return 1;
    }
    int ret = http_request(socket_path, "GET", path, NULL, response, capacity);
    if (ret >= 0 && get_status_code(response) == 200) {
        print_resource_json(get_body(response));
        ret = 0;
    } else {
        if (ret >= 0) {
            fprintf(stderr, "Error: HTTP %d - %s\n", get_status_code(response), get_body(response));
        }
        ret = 1;
    }
    free(response);
    return ret;
}

static int cmd_alloc(int argc, char *argv[], const char *socket_path)
{
    int aicore = -1;
    uint64_t hbm = 0;
    uint64_t hbm_request = 0;
    uint64_t hbm_limit = 0;
    int policy = 0;
    int phy_id = -1;
    int swap_order = 1;
    char allocation_id[ALLOCATION_ID_LEN] = "";
    char pod_uid[128] = "";
    char container[64] = "";

    int i = 2;
    while (i < argc) {
        const char *val = (i + 1 < argc) ? argv[i + 1] : NULL;
        int has_val = (val != NULL);

        if (strcmp(argv[i], "--allocation-id") == 0) {
            if (!has_val || !allocation_id_normalize(val, allocation_id)) {
                fprintf(stderr, "Error: --allocation-id requires a UUID\n");
                return 1;
            }
        } else if (strcmp(argv[i], "-n") == 0 && has_val) {
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
        } else if (strcmp(argv[i], "--swap-order") == 0) {
            if (!has_val || strlen(val) != 1 || val[0] < '0' || val[0] > '2') {
                fprintf(stderr, "Error: --swap-order requires 0, 1 or 2\n");
                return 1;
            }
            swap_order = val[0] - '0';
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

    char allocation_field[64] = "";
    if (allocation_id[0] && snprintf_s(allocation_field, sizeof(allocation_field), sizeof(allocation_field) - 1,
                                       "\"allocation_id\":\"%s\",", allocation_id) < 0) {
        return 1;
    }
    if (hbm_request > 0 && hbm_limit > 0) {
        char body[512];
        int body_len = snprintf_s(body, sizeof(body), sizeof(body) - 1,
                                  "{%s\"aicore_quota\":%d,\"hbm_request\":%lu,\"hbm_limit\":%lu,"
                                  "\"sched_policy\":%d,\"phy_id\":%d,\"pod_uid\":\"%s\","
                                  "\"container_name\":\"%s\",\"swap_order\":%d}",
                                  allocation_field, aicore, hbm_request, hbm_limit, policy, phy_id, pod_uid, container,
                                  swap_order);
        if (body_len < 0 || body_len >= (int)sizeof(body)) {
            fprintf(stderr, "Error: Failed to build request body\n");
            return 1;
        }

        char response[RECV_BUF_SIZE];
        int ret = http_request(socket_path, "POST", "/api/v1/allocate", body, response, sizeof(response));
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
        char allocation_id[64] = "", config_path[512] = "";
        char shm_id[128] = "", minor_name[64] = "";
        char resp_pod_uid[128] = "", resp_container[128] = "";
        char resp_die_id[64] = "";

        parse_string(resp_body, "allocation_id", allocation_id, sizeof(allocation_id));
        parse_string(resp_body, "config_path", config_path, sizeof(config_path));
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
        printf("  Allocation:   %s\n", allocation_id);
        printf("  Config:       %s\n", config_path);
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
                              "{%s\"aicore_quota\":%d,\"hbm_quota\":%lu,\"sched_policy\":%d,"
                              "\"phy_id\":%d,\"pod_uid\":\"%s\",\"container_name\":\"%s\",\"swap_order\":%d}",
                              allocation_field, aicore, hbm_request, policy, phy_id, pod_uid, container, swap_order);
    if (body_len < 0 || body_len >= (int)sizeof(body)) {
        fprintf(stderr, "Error: Failed to build request body\n");
        return 1;
    }

    char response[RECV_BUF_SIZE];
    int ret = http_request(socket_path, "POST", "/api/v1/allocate", body, response, sizeof(response));
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
    char config_path[512] = "";
    char shm_id[128] = "", minor_name[64] = "";
    char resp_pod_uid[128] = "", resp_container[128] = "";
    char die_id[64] = "";

    parse_string(resp_body, "allocation_id", allocation_id, sizeof(allocation_id));
    parse_string(resp_body, "config_path", config_path, sizeof(config_path));
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
    printf("  Allocation:   %s\n", allocation_id);
    printf("  Config:       %s\n", config_path);
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

static void encode_query_value(const char *value, char *out)
{
    const char *hex = "0123456789ABCDEF";
    for (const unsigned char *p = (const unsigned char *)value; *p; p++) {
        if ((*p >= 'a' && *p <= 'z') || (*p >= 'A' && *p <= 'Z') || (*p >= '0' && *p <= '9') || strchr("-._~", *p)) {
            *out++ = (char)*p;
        } else {
            *out++ = '%';
            *out++ = hex[*p >> 4];
            *out++ = hex[*p & 15];
        }
    }
    *out = '\0';
}

static int cmd_release(int argc, char *argv[], const char *socket_path)
{
    char allocation_id[64] = "";
    char pod_uid[128] = "";
    char container[64] = "";

    int i = 2;
    while (i < argc) {
        const char *val = (i + 1 < argc) ? argv[i + 1] : NULL;
        int has_val = (val != NULL);

        if ((strcmp(argv[i], "-a") == 0 || strcmp(argv[i], "--allocation-id") == 0) && has_val) {
            if (strlen(val) >= sizeof(allocation_id) ||
                strncpy_s(allocation_id, sizeof(allocation_id), val, strlen(val)) != 0) {
                fprintf(stderr, "Error: Failed to copy allocation_id\n");
                return 1;
            }
        } else if (strcmp(argv[i], "-u") == 0 && has_val) {
            if (strlen(val) >= sizeof(pod_uid) || strncpy_s(pod_uid, sizeof(pod_uid), val, strlen(val)) != 0) {
                fprintf(stderr, "Error: Failed to copy pod_uid\n");
                return 1;
            }
        } else if (strcmp(argv[i], "-c") == 0 && has_val) {
            if (strlen(val) >= sizeof(container) || strncpy_s(container, sizeof(container), val, strlen(val)) != 0) {
                fprintf(stderr, "Error: Failed to copy container\n");
                return 1;
            }
        } else {
            has_val = 0;
        }

        i += has_val ? 2 : 1;
    }

    int by_id = allocation_id[0] != '\0';
    if (!by_id && (pod_uid[0] == '\0' || container[0] == '\0')) {
        fprintf(stderr, "Error: -a <allocation_id> or both -u <pod_uid> and -c <container> are required\n");
        return 1;
    }

    char encoded_pod[sizeof(pod_uid) * 3], encoded_container[sizeof(container) * 3];
    encode_query_value(pod_uid, encoded_pod);
    encode_query_value(container, encoded_container);
    char path[sizeof(encoded_pod) + sizeof(encoded_container) + 64];
    int path_len = by_id ?
                       snprintf_s(path, sizeof(path), sizeof(path) - 1, "/api/v1/allocations/%s", allocation_id) :
                       snprintf_s(path, sizeof(path), sizeof(path) - 1,
                                  "/api/v1/allocations?pod_uid=%s&container_name=%s", encoded_pod, encoded_container);
    if (path_len < 0 || path_len >= (int)sizeof(path)) {
        fprintf(stderr, "Error: Failed to build request path\n");
        return 1;
    }

    char response[RECV_BUF_SIZE];
    int ret = http_request(socket_path, "DELETE", path, NULL, response, sizeof(response));
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

static int cmd_status(int argc, char *argv[], const char *socket_path)
{
    char response[RECV_BUF_SIZE];
    int ret = http_request(socket_path, "GET", "/api/v1/allocations", NULL, response, sizeof(response));
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

static int cmd_checkpoint(int argc, char *argv[], const char *socket_path)
{
    char response[RECV_BUF_SIZE];
    int ret = http_request(socket_path, "POST", "/api/v1/checkpoint", "{}", response, sizeof(response));
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

static int cmd_recover(int argc, char *argv[], const char *socket_path)
{
    char response[RECV_BUF_SIZE];
    int ret = http_request(socket_path, "POST", "/api/v1/recover", "{}", response, sizeof(response));
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

static int cmd_swap_status(int argc, char *argv[], const char *socket_path)
{
    char response[RECV_BUF_SIZE];
    int ret = http_request(socket_path, "GET", "/api/v1/swap/status", NULL, response, sizeof(response));
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

static int cmd_swap_clean(int argc, char *argv[], const char *socket_path)
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
    int ret = http_request(socket_path, "POST", "/api/v1/swap/clean", body, response, sizeof(response));
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

static int cmd_swap_out(int argc, char *argv[], const char *socket_path)
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
    int ret = http_request(socket_path, "POST", "/api/v1/swap/force-out", body, response, sizeof(response));
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

static int cmd_swap_in(int argc, char *argv[], const char *socket_path)
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
    int ret = http_request(socket_path, "POST", "/api/v1/swap/force-in", body, response, sizeof(response));
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

    const char *socket_path = CLI_DEFAULT_SOCKET_PATH;

    int i = 1;
    while (i < argc) {
        if (strcmp(argv[i], "--socket") == 0) {
            if (i + 1 >= argc || argv[i + 1][0] != '/' ||
                strnlen(argv[i + 1], sizeof(((struct sockaddr_un *)0)->sun_path)) >=
                    sizeof(((struct sockaddr_un *)0)->sun_path)) {
                fprintf(stderr, "Error: --socket requires a valid absolute Unix socket path\n");
                return 1;
            }
            socket_path = argv[i + 1];
            i += 2;
        } else {
            i++;
        }
    }

    /* Also accept the conventional global-option form:
     * enpu-cli --socket /path/to/socket <command> ... */
    if (argc >= 3 && strcmp(argv[1], "--socket") == 0) {
        for (int j = 1; j + 2 < argc; ++j) {
            argv[j] = argv[j + 2];
        }
        argc -= 2;
        if (argc < 2) {
            fprintf(stderr, "Error: command is required\n");
            return 1;
        }
    }

    const char *cmd = argv[1];

    if (strcmp(cmd, "list") == 0) {
        return cmd_list(argc, argv, socket_path);
    } else if (strcmp(cmd, "resources") == 0) {
        return cmd_resources(argc, argv, socket_path);
    } else if (strcmp(cmd, "alloc") == 0) {
        return cmd_alloc(argc, argv, socket_path);
    } else if (strcmp(cmd, "release") == 0) {
        return cmd_release(argc, argv, socket_path);
    } else if (strcmp(cmd, "status") == 0) {
        return cmd_status(argc, argv, socket_path);
    } else if (strcmp(cmd, "checkpoint") == 0) {
        return cmd_checkpoint(argc, argv, socket_path);
    } else if (strcmp(cmd, "recover") == 0) {
        return cmd_recover(argc, argv, socket_path);
    } else if (strcmp(cmd, "swap") == 0) {
        if (argc < 3) {
            fprintf(stderr, "Error: swap subcommand required (status|clean|out|in)\n");
            return 1;
        }
        const char *subcmd = argv[2];
        if (strcmp(subcmd, "status") == 0) {
            return cmd_swap_status(argc, argv, socket_path);
        } else if (strcmp(subcmd, "clean") == 0) {
            return cmd_swap_clean(argc, argv, socket_path);
        } else if (strcmp(subcmd, "out") == 0) {
            return cmd_swap_out(argc, argv, socket_path);
        } else if (strcmp(subcmd, "in") == 0) {
            return cmd_swap_in(argc, argv, socket_path);
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
