/* Copyright (c) Huawei Technologies Co., Ltd. 2026. All rights reserved.
 * Licensed under Mulan PSL v2. */
#define _GNU_SOURCE
#include "resource_format.h"
#include <cjson/cJSON.h>
#include <errno.h>
#include <inttypes.h>
#include <limits.h>
#include <locale.h>
#include <math.h>
#include <stdarg.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include "allocation_id.h"
#include "log.h"
#include "securec.h"

typedef struct text_buffer {
    char *data;
    size_t length;
    size_t capacity;
    bool failed;
} text_buffer_t;

static void append(text_buffer_t *b, const char *format, ...)
{
    while (!b->failed) {
        va_list args;
        va_start(args, format);
        int n = vsnprintf_s(b->data + b->length, b->capacity - b->length, b->capacity - b->length - 1, format, args);
        va_end(args);
        if (n >= 0) {
            b->length += (size_t)n;
            return;
        }
        if (b->capacity >= ENPU_RESOURCE_BODY_LIMIT) {
            b->failed = true;
            return;
        }
        size_t capacity = b->capacity * 2;
        char *data = realloc(b->data, capacity);
        if (data == NULL) {
            b->failed = true;
            return;
        }
        b->data = data;
        b->capacity = capacity;
    }
}

static void quoted(text_buffer_t *b, const char *s)
{
    cJSON *string = cJSON_CreateString(s);
    char *encoded = string == NULL ? NULL : cJSON_PrintUnformatted(string);
    if (encoded == NULL) {
        b->failed = true;
    } else {
        append(b, "%s", encoded);
    }
    cJSON_Delete(string);
    cJSON_free(encoded);
}

static bool valid(enpu_resource_metric_status_t s)
{
    return s == ENPU_RESOURCE_METRIC_OK || s == ENPU_RESOURCE_METRIC_IDLE;
}

static void json_metric(text_buffer_t *b, const char *name, const enpu_resource_metric_t *m)
{
    append(b, "\"%s\":{\"value\":", name);
    if (valid(m->status) && isfinite(m->value) && m->value >= 0 && m->value <= 100) {
        append(b, "%.17g", m->value);
    } else {
        append(b, "null");
        if (valid(m->status)) {
            b->failed = true;
        }
    }
    append(b, ",\"status\":\"%s\"}", enpu_resource_metric_status_string(m->status));
}

static void json_metric_detail(text_buffer_t *b, const char *name, const enpu_resource_metric_detail_t *d)
{
    append(b, "\"%s\":{\"source_result\":%d,\"sampled_at_monotonic_ns\":%" PRIu64 "}", name, d->source_result,
           d->sampled_at_monotonic_ns);
}

#define PHYSICAL_FIELDS(X) \
    X(npu_utilization)     \
    X(aicore_utilization) X(aicpu_utilization) X(ctrlcpu_utilization) X(cube_utilization) X(vector_utilization)

static void json_detail(text_buffer_t *b, const enpu_resource_detail_t *d)
{
    append(b, ",\"detail\":{\"window_ms\":%u,\"collected_at_ns\":%" PRIu64 ",\"die_id\":", d->window_ms,
           d->collected_at_ns);
    quoted(b, d->die_id);
    append(b, ",\"shm_id\":");
    quoted(b, d->shm_id);
    append(b,
           ",\"raw\":{\"sum_block_dim\":%" PRIu64 ",\"launch_count\":%" PRIu64 ",\"avg_kernel_duration_ns\":%" PRIu64
           "},\"calculated\":{\"work\":%.17g,\"weight\":%.17g},"
           "\"hbm\":{\"source_result\":%d,\"sampled_at_monotonic_ns\":%" PRIu64 "},\"physical\":{",
           d->sum_block_dim, d->launch_count, d->avg_kernel_duration_ns, d->work, d->weight, d->hbm_source_result,
           d->hbm_sampled_at_monotonic_ns);
    bool first = true;
#define DETAIL_FIELD(name) \
    if (!first) {          \
        append(b, ",");    \
    }                      \
    first = false;         \
    json_metric_detail(b, #name, &d->physical.name);
    PHYSICAL_FIELDS(DETAIL_FIELD)
#undef DETAIL_FIELD
    append(b, "},\"vnpu\":{");
    json_metric_detail(b, "utilization", &d->vnpu.utilization);
    append(b, ",");
    json_metric_detail(b, "aicore_utilization", &d->vnpu.aicore_utilization);
    append(b, "}}");
}

static void format_json(text_buffer_t *b, const enpu_resource_response_t *r)
{
    append(b, "{\"items\":[");
    for (size_t i = 0; i < r->count; ++i) {
        const enpu_resource_info_t *s = &r->items[i];
        append(b, "%s{\"allocation_id\":", i ? "," : "");
        quoted(b, s->allocation_id);
        append(b, ",\"phy_id\":%d,\"vnpu_id\":%d,\"metadata\":{\"pod_uid\":", s->phy_id, s->vnpu_id);
        quoted(b, s->metadata.pod_uid);
        append(b, ",\"container_name\":");
        quoted(b, s->metadata.container_name);
        append(b, "}");
        const char *policy = s->quota.scheduling_policy == SCHED_POLICY_FIXED_SHARE ? "fixed-share" :
                             s->quota.scheduling_policy == SCHED_POLICY_ELASTIC     ? "elastic" :
                                                                                      "best-effort";
        append(b, ",\"quota\":{\"aicore_percent\":");
        if (s->quota.core_limit_enabled) {
            append(b, "%d", s->quota.aicore_percent);
        } else {
            append(b, "null");
        }
        append(b,
               ",\"core_limit_enabled\":%s,\"scheduling_policy\":\"%s\"},"
               "\"hbm\":{\"request_bytes\":%" PRIu64 ",\"limit_bytes\":%" PRIu64 ",\"usage\":{\"used_bytes\":",
               s->quota.core_limit_enabled ? "true" : "false", policy, s->hbm.request_bytes, s->hbm.limit_bytes);
        if (valid(s->hbm.usage.status)) {
            append(b, "%" PRIu64, s->hbm.usage.used_bytes);
        } else {
            append(b, "null");
        }
        append(b, ",\"status\":\"%s\"}},\"physical\":{", enpu_resource_metric_status_string(s->hbm.usage.status));
        bool first = true;
#define METRIC_FIELD(name) \
    if (!first) {          \
        append(b, ",");    \
    }                      \
    first = false;         \
    json_metric(b, #name, &s->physical.name);
        PHYSICAL_FIELDS(METRIC_FIELD)
#undef METRIC_FIELD
        append(b, "},\"vnpu\":{");
        json_metric(b, "utilization", &s->vnpu.utilization);
        append(b, ",");
        json_metric(b, "aicore_utilization", &s->vnpu.aicore_utilization);
        append(b, "}");
        if (s->detail != NULL) {
            json_detail(b, s->detail);
        }
        append(b, "}");
    }
    append(b, "],\"result\":0,\"error_msg\":\"\"}\n");
}

static void family(text_buffer_t *b, const char *name, const char *help)
{
    append(b, "# HELP %s %s\n# TYPE %s gauge\n", name, help, name);
}

static void prom_metric(text_buffer_t *b, const char *name, const char *labels, const char *valid_family,
                        const char *key, const enpu_resource_metric_t *m, uint64_t timestamp_ms)
{
    bool ok = valid(m->status) && isfinite(m->value) && m->value >= 0 && m->value <= 100;
    append(b, "%s{%s,metric=\"%s\"} %d %" PRIu64 "\n", valid_family, labels, key, ok, timestamp_ms);
    if (ok) {
        append(b, "%s{%s} %.17g %" PRIu64 "\n", name, labels, m->value, timestamp_ms);
    }
}

static int realtime_ms(uint64_t *timestamp_ms)
{
    struct timespec now;
    if (timestamp_ms == NULL || clock_gettime(CLOCK_REALTIME, &now) != 0 || now.tv_sec < 0) {
        return ENPU_FAIL;
    }
    *timestamp_ms = (uint64_t)now.tv_sec * 1000ULL + (uint64_t)now.tv_nsec / 1000000ULL;
    return ENPU_SUCCESS;
}

static bool prom_escape_label(const char *source, char *target, size_t capacity)
{
    size_t out = 0;
    for (const unsigned char *p = (const unsigned char *)source; *p != '\0'; ++p) {
        const char *escape = NULL;
        if (*p == '\\') {
            escape = "\\\\";
        } else if (*p == '"') {
            escape = "\\\"";
        } else if (*p == '\n') {
            escape = "\\n";
        }
        if (escape != NULL) {
            if (out + 2 >= capacity) {
                return false;
            }
            target[out++] = escape[0];
            target[out++] = escape[1];
        } else {
            if (out + 1 >= capacity) {
                return false;
            }
            target[out++] = (char)*p;
        }
    }
    target[out] = '\0';
    return true;
}

static bool allocation_labels(const enpu_resource_info_t *item, char *labels, size_t capacity)
{
    char pod_uid[MAX_UUID_LEN * 2] = {0};
    char container_name[MAX_NAME_LEN * 2] = {0};
    if (!prom_escape_label(item->metadata.pod_uid, pod_uid, sizeof(pod_uid)) ||
        !prom_escape_label(item->metadata.container_name, container_name, sizeof(container_name))) {
        return false;
    }
    int length = snprintf_s(labels, capacity, capacity - 1,
                            "allocation_id=\"%s\",phy_id=\"%d\",vnpu_id=\"%d\",pod_uid=\"%s\","
                            "container_name=\"%s\"",
                            item->allocation_id, item->phy_id, item->vnpu_id, pod_uid, container_name);
    return length >= 0;
}

static void format_prometheus(text_buffer_t *b, const enpu_resource_response_t *r, uint64_t timestamp_ms)
{
    const char *physical_names[] = {"enpu_npu_utilization_percent",       "enpu_npu_aicore_utilization_percent",
                                    "enpu_npu_aicpu_utilization_percent", "enpu_npu_ctrlcpu_utilization_percent",
                                    "enpu_npu_cube_utilization_percent",  "enpu_npu_vector_utilization_percent"};
    const char *keys[] = {"utilization_percent",         "aicore_utilization_percent", "aicpu_utilization_percent",
                          "ctrlcpu_utilization_percent", "cube_utilization_percent",   "vector_utilization_percent"};
    for (size_t i = 0; i < sizeof(physical_names) / sizeof(physical_names[0]); ++i) {
        family(b, physical_names[i], "Physical device utilization percentage.");
    }
    family(b, "enpu_vnpu_utilization_percent", "Estimated vNPU utilization percentage.");
    family(b, "enpu_vnpu_aicore_utilization_percent", "Estimated vNPU AI Core utilization percentage.");
    family(b, "enpu_vnpu_aicore_quota_percent", "Configured AI Core quota percentage when limiting is enabled.");
    family(b, "enpu_vnpu_core_limit_enabled", "Whether AI Core limiting is enabled.");
    family(b, "enpu_vnpu_hbm_request_bytes", "Configured HBM request in bytes.");
    family(b, "enpu_vnpu_hbm_limit_bytes", "Configured HBM limit in bytes.");
    family(b, "enpu_vnpu_hbm_used_bytes", "Source container HBM usage on the configured device in bytes.");
    family(b, "enpu_npu_metric_valid", "Whether a physical metric has a valid current sample.");
    family(b, "enpu_vnpu_metric_valid", "Whether a resource metric has a valid current sample.");
    family(b, "enpu_allocation_info", "Allocation identity, metadata and assigned resource.");
    for (size_t i = 0; i < r->count; ++i) {
        const enpu_resource_info_t *s = &r->items[i];
        bool first_chip = true;
        for (size_t j = 0; j < i; ++j) {
            if (r->items[j].phy_id == s->phy_id) {
                first_chip = false;
                break;
            }
        }
        char labels[96];
        if (snprintf_s(labels, sizeof(labels), sizeof(labels) - 1, "phy_id=\"%d\"", s->phy_id) < 0) {
            b->failed = true;
            return;
        }
        if (first_chip) {
            const enpu_resource_metric_t *fields[] = {&s->physical.npu_utilization,   &s->physical.aicore_utilization,
                                                      &s->physical.aicpu_utilization, &s->physical.ctrlcpu_utilization,
                                                      &s->physical.cube_utilization,  &s->physical.vector_utilization};
            for (size_t j = 0; j < sizeof(fields) / sizeof(fields[0]); ++j) {
                prom_metric(b, physical_names[j], labels, "enpu_npu_metric_valid", keys[j], fields[j], timestamp_ms);
            }
        }
        char allocation_label_set[768];
        if (!allocation_labels(s, allocation_label_set, sizeof(allocation_label_set))) {
            b->failed = true;
            return;
        }
        append(b, "enpu_allocation_info{%s} 1 %" PRIu64 "\n", allocation_label_set, timestamp_ms);
        append(b, "enpu_vnpu_core_limit_enabled{%s} %d %" PRIu64 "\n", allocation_label_set,
               s->quota.core_limit_enabled, timestamp_ms);
        if (s->quota.core_limit_enabled) {
            append(b, "enpu_vnpu_aicore_quota_percent{%s} %d %" PRIu64 "\n", allocation_label_set,
                   s->quota.aicore_percent, timestamp_ms);
        }
        append(b, "enpu_vnpu_hbm_request_bytes{%s} %" PRIu64 " %" PRIu64 "\n", allocation_label_set,
               s->hbm.request_bytes, timestamp_ms);
        append(b, "enpu_vnpu_hbm_limit_bytes{%s} %" PRIu64 " %" PRIu64 "\n", allocation_label_set, s->hbm.limit_bytes,
               timestamp_ms);
        append(b, "enpu_vnpu_metric_valid{%s,metric=\"hbm_used_bytes\"} %d %" PRIu64 "\n", allocation_label_set,
               valid(s->hbm.usage.status), timestamp_ms);
        if (valid(s->hbm.usage.status)) {
            append(b, "enpu_vnpu_hbm_used_bytes{%s} %" PRIu64 " %" PRIu64 "\n", allocation_label_set,
                   s->hbm.usage.used_bytes, timestamp_ms);
        }
        prom_metric(b, "enpu_vnpu_utilization_percent", allocation_label_set, "enpu_vnpu_metric_valid",
                    "utilization_percent", &s->vnpu.utilization, timestamp_ms);
        prom_metric(b, "enpu_vnpu_aicore_utilization_percent", allocation_label_set, "enpu_vnpu_metric_valid",
                    "aicore_utilization_percent", &s->vnpu.aicore_utilization, timestamp_ms);
    }
}

int resource_format(const enpu_resource_response_t *r, bool metrics, char **output)
{
    if (output == NULL) {
        return ENPU_INVALID_PARAM;
    }
    *output = NULL;
    if (r == NULL || (r->count != 0 && r->items == NULL)) {
        return ENPU_INVALID_PARAM;
    }
    text_buffer_t b = {.capacity = 4096};
    b.data = malloc(b.capacity);
    if (b.data == NULL) {
        LOG_ERROR("[RESOURCE-FORMAT] Initial buffer allocation failed: bytes=%zu", b.capacity);
        return ENPU_NO_RESOURCE;
    }
    /* Do not change the process locale or round uint64_t byte counters through double. */
    locale_t locale = newlocale(LC_NUMERIC_MASK, "C", (locale_t)0);
    locale_t previous = locale != (locale_t)0 ? uselocale(locale) : (locale_t)0;
    if (locale == (locale_t)0 || previous == (locale_t)0) {
        free(b.data);
        if (locale != (locale_t)0) {
            freelocale(locale);
        }
        return ENPU_FAIL;
    }
    if (metrics) {
        uint64_t timestamp_ms = 0;
        if (realtime_ms(&timestamp_ms) != ENPU_SUCCESS) {
            LOG_ERROR("[RESOURCE-FORMAT] Failed to read CLOCK_REALTIME for Prometheus timestamp: errno=%d", errno);
            b.failed = true;
        } else {
            format_prometheus(&b, r, timestamp_ms);
        }
    } else {
        format_json(&b, r);
    }
    uselocale(previous);
    freelocale(locale);
    if (b.failed) {
        LOG_ERROR("[RESOURCE-FORMAT] Formatting failed: metrics=%d resources=%zu used=%zu capacity=%zu limit=%u; "
                  "allocation, output limit or invalid sample",
                  metrics, r->count, b.length, b.capacity, ENPU_RESOURCE_BODY_LIMIT);
        free(b.data);
        return ENPU_FAIL;
    }
    *output = b.data;
    return ENPU_SUCCESS;
}

static int parse_id(const char *value, int32_t *id)
{
    if (*value == '\0') {
        return ENPU_INVALID_PARAM;
    }
    for (const char *p = value; *p; ++p) {
        if (*p < '0' || *p > '9') {
            return ENPU_INVALID_PARAM;
        }
    }
    errno = 0;
    char *end = NULL;
    long n = strtol(value, &end, 10);
    if (errno || *end || n > INT32_MAX) {
        return ENPU_INVALID_PARAM;
    }
    *id = (int32_t)n;
    return ENPU_SUCCESS;
}

static int hex_value(char value)
{
    if (value >= '0' && value <= '9') {
        return value - '0';
    }
    if (value >= 'a' && value <= 'f') {
        return value - 'a' + 10;
    }
    if (value >= 'A' && value <= 'F') {
        return value - 'A' + 10;
    }
    return -1;
}

static int parse_text(const char *value, char *output, size_t capacity)
{
    size_t length = 0;
    if (*value == '\0') {
        return ENPU_INVALID_PARAM;
    }
    while (*value != '\0') {
        unsigned char decoded = (unsigned char)*value++;
        if (decoded == '%') {
            int high = hex_value(value[0]);
            int low = value[0] == '\0' ? -1 : hex_value(value[1]);
            if (high < 0 || low < 0) {
                return ENPU_INVALID_PARAM;
            }
            decoded = (unsigned char)((high << 4) | low);
            value += 2;
        } else if (decoded == '+') {
            decoded = ' ';
        }
        if (decoded == '\0' || decoded < 0x20 || length + 1 >= capacity) {
            return ENPU_INVALID_PARAM;
        }
        output[length++] = (char)decoded;
    }
    output[length] = '\0';
    return ENPU_SUCCESS;
}

int resource_parse_uri(const char *uri, enpu_resource_query_t *query, bool *metrics)
{
    enum
    {
        PARAM_PHY_ID = 1U << 0,
        PARAM_VNPU_ID = 1U << 1,
        PARAM_DETAIL = 1U << 2,
        PARAM_ALLOCATION_ID = 1U << 3,
        PARAM_POD_UID = 1U << 4,
        PARAM_CONTAINER_NAME = 1U << 5,
    };
    if (uri == NULL || query == NULL || metrics == NULL) {
        return ENPU_INVALID_PARAM;
    }
    *query = (enpu_resource_query_t){.type = ENPU_RESOURCE_QUERY_ALL};
    *metrics = strcmp(uri, "/metrics") == 0;
    if (*metrics) {
        return ENPU_SUCCESS;
    }
    const char *base = "/api/v1/resources";
    size_t length = strlen(base);
    if (strncmp(uri, base, length) != 0 || (uri[length] != '\0' && uri[length] != '?')) {
        return ENPU_INVALID_PARAM;
    }
    if (uri[length] == '\0') {
        return ENPU_SUCCESS;
    }
    char params[1024];
    if (strcpy_s(params, sizeof(params), uri + length + 1) != 0 || params[0] == '\0') {
        return ENPU_INVALID_PARAM;
    }
    unsigned int seen = 0;
    int32_t phy = -1, vnpu = -1;
    char allocation_id[ALLOCATION_ID_LEN] = {0};
    char pod_uid[MAX_UUID_LEN] = {0};
    char container_name[MAX_NAME_LEN] = {0};
    char *part = params;
    while (part != NULL) {
        char *next = strchr(part, '&');
        if (next != NULL) {
            *next++ = '\0';
        }
        char *eq = strchr(part, '=');
        if (eq == NULL) {
            return ENPU_INVALID_PARAM;
        }
        *eq++ = '\0';
        unsigned int flag = strcmp(part, "phy_id") == 0         ? PARAM_PHY_ID :
                            strcmp(part, "vnpu_id") == 0        ? PARAM_VNPU_ID :
                            strcmp(part, "detail") == 0         ? PARAM_DETAIL :
                            strcmp(part, "allocation_id") == 0  ? PARAM_ALLOCATION_ID :
                            strcmp(part, "pod_uid") == 0        ? PARAM_POD_UID :
                            strcmp(part, "container_name") == 0 ? PARAM_CONTAINER_NAME :
                                                                  0;
        if (flag == 0 || (seen & flag)) {
            return ENPU_INVALID_PARAM;
        }
        seen |= flag;
        if (flag == PARAM_DETAIL) {
            if (strcmp(eq, "true") != 0 && strcmp(eq, "false") != 0) {
                return ENPU_INVALID_PARAM;
            }
            query->detail = strcmp(eq, "true") == 0;
        } else if (flag == PARAM_PHY_ID || flag == PARAM_VNPU_ID) {
            if (parse_id(eq, flag == PARAM_PHY_ID ? &phy : &vnpu) != ENPU_SUCCESS) {
                return ENPU_INVALID_PARAM;
            }
        } else {
            char *target = flag == PARAM_ALLOCATION_ID ? allocation_id :
                           flag == PARAM_POD_UID       ? pod_uid :
                                                         container_name;
            size_t capacity = flag == PARAM_ALLOCATION_ID ? sizeof(allocation_id) :
                              flag == PARAM_POD_UID       ? sizeof(pod_uid) :
                                                            sizeof(container_name);
            if (parse_text(eq, target, capacity) != ENPU_SUCCESS) {
                return ENPU_INVALID_PARAM;
            }
        }
        part = next;
    }
    if ((seen & PARAM_VNPU_ID) && !(seen & PARAM_PHY_ID)) {
        return ENPU_INVALID_PARAM;
    }
    unsigned int coordinate = seen & (PARAM_PHY_ID | PARAM_VNPU_ID);
    unsigned int allocation = seen & PARAM_ALLOCATION_ID;
    unsigned int metadata = seen & (PARAM_POD_UID | PARAM_CONTAINER_NAME);
    if ((allocation && (coordinate || metadata)) || (coordinate && metadata)) {
        return ENPU_INVALID_PARAM;
    }
    if (allocation) {
        if (!allocation_id_is_valid(allocation_id) ||
            strcpy_s(query->selector.allocation_id, sizeof(query->selector.allocation_id), allocation_id) != 0) {
            return ENPU_INVALID_PARAM;
        }
        query->type = ENPU_RESOURCE_QUERY_BY_ALLOCATION;
    } else if (metadata) {
        if ((pod_uid[0] != '\0' &&
             strcpy_s(query->selector.metadata.pod_uid, sizeof(query->selector.metadata.pod_uid), pod_uid) != 0) ||
            (container_name[0] != '\0' &&
             strcpy_s(query->selector.metadata.container_name, sizeof(query->selector.metadata.container_name),
                      container_name) != 0)) {
            return ENPU_INVALID_PARAM;
        }
        query->type = ENPU_RESOURCE_QUERY_BY_METADATA;
    } else if (seen & PARAM_VNPU_ID) {
        query->type = ENPU_RESOURCE_QUERY_BY_RESOURCE;
        query->selector.resource.phy_id = phy;
        query->selector.resource.vnpu_id = vnpu;
    } else if (seen & PARAM_PHY_ID) {
        query->type = ENPU_RESOURCE_QUERY_BY_PHY;
        query->selector.phy_id = phy;
    }
    return ENPU_SUCCESS;
}
