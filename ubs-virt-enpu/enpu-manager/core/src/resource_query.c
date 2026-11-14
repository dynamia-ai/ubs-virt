/*
 * Copyright (c) Huawei Technologies Co., Ltd. 2026. All rights reserved.
 * enpu-manager is licensed under Mulan PSL v2.
 */
#include "enpu_manager.h"

#include <errno.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include "allocation_id.h"
#include "dcmi_interface_api.h"
#include "dcmi_wrapper.h"
#include "log.h"
#include "securec.h"
#include "vnpu_metrics_reader.h"
#include "vnpu_utilization_calculator.h"

#define RESOURCE_MB_TO_BYTES (1024ULL * 1024ULL)

typedef struct resource_physical_snapshot {
    int32_t phy_id;
    dcmi_resource_metrics_t metrics;
    int result;
} resource_physical_snapshot_t;

typedef struct resource_group {
    const allocation_t *source;
    vnpu_stats_snapshot_t snapshot;
    vnpu_stats_reader_status_t reader_status;
    double total_work;
    bool missing_duration;
    bool inconsistent_domain;
    bool ambiguous_chip;
} resource_group_t;

static uint64_t resource_now_ns(void)
{
    struct timespec ts;
    if (clock_gettime(CLOCK_MONOTONIC, &ts) != 0) {
        LOG_WARN("[RESOURCES] Monotonic clock unavailable: errno=%d", errno);
        return 0;
    }
    return (uint64_t)ts.tv_sec * 1000000000ULL + (uint64_t)ts.tv_nsec;
}

const char *enpu_resource_metric_status_string(enpu_resource_metric_status_t status)
{
    switch (status) {
        case ENPU_RESOURCE_METRIC_OK:
            return "ok";
        case ENPU_RESOURCE_METRIC_IDLE:
            return "idle";
        case ENPU_RESOURCE_METRIC_UNSUPPORTED:
            return "unsupported";
        case ENPU_RESOURCE_METRIC_UNAVAILABLE:
            return "unavailable";
        case ENPU_RESOURCE_METRIC_STALE:
            return "stale";
        case ENPU_RESOURCE_METRIC_INCONSISTENT:
            return "inconsistent";
        case ENPU_RESOURCE_METRIC_STATS_SHM_MISSING:
            return "stats_shm_missing";
        case ENPU_RESOURCE_METRIC_STATS_VERSION_MISMATCH:
            return "stats_version_mismatch";
        case ENPU_RESOURCE_METRIC_STATS_UNAVAILABLE:
            return "stats_unavailable";
        case ENPU_RESOURCE_METRIC_AVG_DURATION_MISSING:
            return "avg_duration_missing";
        case ENPU_RESOURCE_METRIC_DCMI_UNAVAILABLE:
            return "dcmi_unavailable";
        case ENPU_RESOURCE_METRIC_VNPU_ID_OUT_OF_RANGE:
            return "vnpu_id_out_of_range";
        default:
            return "unknown";
    }
}

void enpu_free_resource_response(enpu_resource_response_t *response)
{
    if (response == NULL) {
        return;
    }
    if (response->items != NULL) {
        for (size_t i = 0; i < response->count; ++i) {
            free(response->items[i].detail);
        }
        free(response->items);
    }
    response->items = NULL;
    response->count = 0;
}

static int validate_resource_query(const enpu_resource_query_t *query)
{
    if (query == NULL || query->type == ENPU_RESOURCE_QUERY_ALL) {
        return ENPU_SUCCESS;
    }
    switch (query->type) {
        case ENPU_RESOURCE_QUERY_BY_PHY:
            return query->selector.phy_id >= 0 ? ENPU_SUCCESS : ENPU_INVALID_PARAM;
        case ENPU_RESOURCE_QUERY_BY_RESOURCE:
            return query->selector.resource.phy_id >= 0 && query->selector.resource.vnpu_id >= 0 ? ENPU_SUCCESS :
                                                                                                   ENPU_INVALID_PARAM;
        case ENPU_RESOURCE_QUERY_BY_ALLOCATION:
            return allocation_id_is_valid(query->selector.allocation_id) ? ENPU_SUCCESS : ENPU_INVALID_PARAM;
        case ENPU_RESOURCE_QUERY_BY_METADATA:
            if (strnlen(query->selector.metadata.pod_uid, MAX_UUID_LEN) == MAX_UUID_LEN ||
                strnlen(query->selector.metadata.container_name, MAX_NAME_LEN) == MAX_NAME_LEN) {
                return ENPU_INVALID_PARAM;
            }
            return query->selector.metadata.pod_uid[0] != '\0' || query->selector.metadata.container_name[0] != '\0' ?
                       ENPU_SUCCESS :
                       ENPU_INVALID_PARAM;
        default:
            return ENPU_INVALID_PARAM;
    }
}

static bool query_requires_match(const enpu_resource_query_t *query)
{
    return query != NULL &&
           (query->type == ENPU_RESOURCE_QUERY_BY_RESOURCE || query->type == ENPU_RESOURCE_QUERY_BY_ALLOCATION);
}

static bool matches_query(const allocation_t *allocation, const enpu_resource_query_t *query)
{
    if (query == NULL || query->type == ENPU_RESOURCE_QUERY_ALL) {
        return true;
    }
    if (query->type == ENPU_RESOURCE_QUERY_BY_PHY) {
        return allocation->phy_id == query->selector.phy_id;
    }
    if (query->type == ENPU_RESOURCE_QUERY_BY_RESOURCE) {
        return allocation->phy_id == query->selector.resource.phy_id &&
               allocation->vnpu_id == query->selector.resource.vnpu_id;
    }
    if (query->type == ENPU_RESOURCE_QUERY_BY_ALLOCATION) {
        return strcmp(allocation->allocation_id, query->selector.allocation_id) == 0;
    }
    return (query->selector.metadata.pod_uid[0] == '\0' ||
            strcmp(allocation->pod_uid, query->selector.metadata.pod_uid) == 0) &&
           (query->selector.metadata.container_name[0] == '\0' ||
            strcmp(allocation->container_name, query->selector.metadata.container_name) == 0);
}

static int copy_resource_text(char *dst, size_t capacity, const char *src)
{
    size_t length = strnlen(src, capacity);
    if (length == capacity || strncpy_s(dst, capacity, src, length) != 0) {
        return ENPU_FAIL;
    }
    return ENPU_SUCCESS;
}

static int initialize_resource(enpu_resource_info_t *item, const allocation_t *allocation, bool detail,
                               uint64_t collected_at_ns)
{
    if (!allocation_id_is_valid(allocation->allocation_id) || allocation->phy_id < 0 || allocation->vnpu_id < 0 ||
        allocation->aicore_quota < 0 || allocation->aicore_quota > HUNDRED_CORE ||
        allocation->sched_policy < SCHED_POLICY_FIXED_SHARE || allocation->sched_policy > SCHED_POLICY_BEST_EFFORT ||
        allocation->hbm_quota > allocation->hbm_limit || allocation->hbm_limit > UINT64_MAX / RESOURCE_MB_TO_BYTES ||
        allocation->shm_id[0] == '\0' ||
        strnlen(allocation->shm_id, sizeof(allocation->shm_id)) == sizeof(allocation->shm_id) ||
        strnlen(allocation->die_id, sizeof(allocation->die_id)) == sizeof(allocation->die_id)) {
        LOG_ERROR("[RESOURCES] Invalid allocation quota or identity: phy=%d vnpu=%d aicore=%d policy=%d "
                  "hbm_request_mb=%llu hbm_limit_mb=%llu shm_length=%zu die_length=%zu",
                  allocation->phy_id, allocation->vnpu_id, allocation->aicore_quota, allocation->sched_policy,
                  (unsigned long long)allocation->hbm_quota, (unsigned long long)allocation->hbm_limit,
                  strnlen(allocation->shm_id, sizeof(allocation->shm_id)),
                  strnlen(allocation->die_id, sizeof(allocation->die_id)));
        return ENPU_FAIL;
    }
    if (copy_resource_text(item->allocation_id, sizeof(item->allocation_id), allocation->allocation_id) !=
            ENPU_SUCCESS ||
        copy_resource_text(item->metadata.pod_uid, sizeof(item->metadata.pod_uid), allocation->pod_uid) !=
            ENPU_SUCCESS ||
        copy_resource_text(item->metadata.container_name, sizeof(item->metadata.container_name),
                           allocation->container_name) != ENPU_SUCCESS) {
        LOG_ERROR("[RESOURCES] Failed to copy allocation identity: allocation_id=%.*s", ALLOCATION_ID_LEN - 1,
                  allocation->allocation_id);
        return ENPU_FAIL;
    }
    item->phy_id = allocation->phy_id;
    item->vnpu_id = allocation->vnpu_id;
    item->quota.aicore_percent = allocation->aicore_quota;
    item->quota.scheduling_policy = (schedule_policy_t)allocation->sched_policy;
    item->quota.core_limit_enabled = allocation->sched_policy != SCHED_POLICY_BEST_EFFORT;
    item->hbm.request_bytes = allocation->hbm_quota * RESOURCE_MB_TO_BYTES;
    item->hbm.limit_bytes = allocation->hbm_limit * RESOURCE_MB_TO_BYTES;
    /* Usage is filled from the container producer's stats snapshot, never host totals. */
    item->hbm.usage.status = ENPU_RESOURCE_METRIC_UNAVAILABLE;
    item->physical.npu_utilization.status = ENPU_RESOURCE_METRIC_UNAVAILABLE;
    item->physical.aicore_utilization.status = ENPU_RESOURCE_METRIC_UNAVAILABLE;
    item->physical.aicpu_utilization.status = ENPU_RESOURCE_METRIC_UNAVAILABLE;
    item->physical.ctrlcpu_utilization.status = ENPU_RESOURCE_METRIC_UNAVAILABLE;
    item->physical.cube_utilization.status = ENPU_RESOURCE_METRIC_UNAVAILABLE;
    item->physical.vector_utilization.status = ENPU_RESOURCE_METRIC_UNAVAILABLE;
    item->vnpu.utilization.status = ENPU_RESOURCE_METRIC_UNAVAILABLE;
    item->vnpu.aicore_utilization.status = ENPU_RESOURCE_METRIC_UNAVAILABLE;
    if (!detail) {
        return ENPU_SUCCESS;
    }
    item->detail = calloc(1, sizeof(*item->detail));
    if (item->detail == NULL) {
        LOG_ERROR("[RESOURCES] Failed to allocate detail: phy=%d vnpu=%d bytes=%zu", item->phy_id, item->vnpu_id,
                  sizeof(*item->detail));
        return ENPU_NO_RESOURCE;
    }
    item->detail->window_ms = (uint32_t)(VNPU_STATS_WINDOW_NS / VNPU_STATS_NS_PER_MS);
    item->detail->collected_at_ns = collected_at_ns;
    if (copy_resource_text(item->detail->die_id, sizeof(item->detail->die_id), allocation->die_id) != ENPU_SUCCESS ||
        copy_resource_text(item->detail->shm_id, sizeof(item->detail->shm_id), allocation->shm_id) != ENPU_SUCCESS) {
        LOG_ERROR("[RESOURCES] Failed to copy detail identity: phy=%d vnpu=%d", item->phy_id, item->vnpu_id);
        return ENPU_FAIL;
    }
    return ENPU_SUCCESS;
}

static void initialize_physical(resource_physical_snapshot_t *physical, int32_t phy_id)
{
    physical->phy_id = phy_id;
    /* The NPU tree stores its enumerated software logic ID as allocation.phy_id.
     * DCMI owns all platform-specific address translation. */
    physical->result = dcmi_query_resource_metrics(phy_id, &physical->metrics);
    LOG_DEBUG("[RESOURCES] Physical snapshot ready: phy=%d ret=%d", phy_id, physical->result);
}

static void initialize_group(resource_group_t *group, const allocation_t *source, const allocation_t *allocations,
                             int count)
{
    uint64_t started_at_ns = resource_now_ns();
    group->source = source;
    for (int i = 0; i < count; ++i) {
        bool same_domain = strncmp(allocations[i].shm_id, source->shm_id, SHM_ID_LEN) == 0;
        if (allocations[i].phy_id == source->phy_id && !same_domain) {
            group->inconsistent_domain = true;
        }
        if (allocations[i].phy_id != source->phy_id && same_domain) {
            group->ambiguous_chip = true;
            group->inconsistent_domain = true;
        }
    }
    if (group->ambiguous_chip) {
        LOG_WARN("[RESOURCES] Stats domain assigned to multiple physical chips: shm=%s; HBM identity is ambiguous",
                 source->shm_id);
    } else if (group->inconsistent_domain) {
        LOG_WARN("[RESOURCES] Incomplete attribution domain: phy=%d shm=%s; vNPU attribution is inconsistent",
                 source->phy_id, source->shm_id);
    }
    group->reader_status = vnpu_metrics_reader_snapshot(source->shm_id, &group->snapshot);
    if (group->reader_status != VNPU_STATS_READER_OK) {
        if (group->reader_status == VNPU_STATS_READER_MISSING) {
            LOG_DEBUG("[RESOURCES] Stats object missing: phy=%d shm=%s", source->phy_id, source->shm_id);
        } else {
            LOG_WARN("[RESOURCES] Stats snapshot failed: phy=%d shm=%s reader_status=%d", source->phy_id,
                     source->shm_id, group->reader_status);
        }
        return;
    }
    /* Include every active contributor, not just resources selected by the query. */
    for (int i = 0; i < VNPU_STATS_MAX_VNPU; ++i) {
        const vnpu_raw_metric_t *metric = &group->snapshot.metrics[i];
        if (metric->sum_block_dim != 0 && metric->avg_kernel_duration_ns == 0) {
            group->missing_duration = true;
            LOG_DEBUG("[RESOURCES] Active contributor missing duration: phy=%d shm=%s vnpu=%d blocks=%llu "
                      "launches=%llu",
                      source->phy_id, source->shm_id, i, (unsigned long long)metric->sum_block_dim,
                      (unsigned long long)metric->launch_count);
        }
        group->total_work += vnpu_utilization_work(metric->sum_block_dim, metric->avg_kernel_duration_ns);
    }
    uint64_t finished_at_ns = resource_now_ns();
    LOG_DEBUG(
        "[RESOURCES] Group snapshot ready: phy=%d shm=%s snapshot_ns=%llu total_work=%.3f "
        "missing_duration=%d ambiguous_chip=%d elapsed_ns=%llu",
        source->phy_id, source->shm_id, (unsigned long long)group->snapshot.collected_at_ns, group->total_work,
        group->missing_duration, group->ambiguous_chip,
        (unsigned long long)(started_at_ns != 0 && finished_at_ns >= started_at_ns ? finished_at_ns - started_at_ns :
                                                                                     0));
}

static void set_physical_metric(enpu_resource_metric_t *metric, enpu_resource_metric_detail_t *detail,
                                const dcmi_metric_t *source, int32_t phy_id, const char *name)
{
    metric->status = ENPU_RESOURCE_METRIC_DCMI_UNAVAILABLE;
    if (detail != NULL) {
        detail->source_result = source->source_result;
        detail->sampled_at_monotonic_ns = source->sampled_at_monotonic_ns;
    }
    switch (source->status) {
        case DCMI_METRIC_OK:
            if (source->value <= HUNDRED_CORE) {
                metric->value = (double)source->value;
                metric->status = ENPU_RESOURCE_METRIC_OK;
            } else {
                metric->status = ENPU_RESOURCE_METRIC_INCONSISTENT;
                LOG_WARN("[RESOURCES] Invalid physical utilization: phy=%d metric=%s value=%u", phy_id, name,
                         source->value);
            }
            break;
        case DCMI_METRIC_UNSUPPORTED:
            metric->status = ENPU_RESOURCE_METRIC_UNSUPPORTED;
            break;
        case DCMI_METRIC_INCONSISTENT:
            metric->status = ENPU_RESOURCE_METRIC_INCONSISTENT;
            break;
        case DCMI_METRIC_UNAVAILABLE:
        default:
            break;
    }
}

static enpu_resource_metric_status_t work_status(const resource_group_t *group, int vnpu_id)
{
    if (vnpu_id >= VNPU_STATS_MAX_VNPU) {
        LOG_WARN("[RESOURCES] vNPU exceeds stats capacity: phy=%d vnpu=%d capacity=%d", group->source->phy_id, vnpu_id,
                 VNPU_STATS_MAX_VNPU);
        return ENPU_RESOURCE_METRIC_VNPU_ID_OUT_OF_RANGE;
    }
    switch (group->reader_status) {
        case VNPU_STATS_READER_MISSING:
            return ENPU_RESOURCE_METRIC_STATS_SHM_MISSING;
        case VNPU_STATS_READER_VERSION_MISMATCH:
            return ENPU_RESOURCE_METRIC_STATS_VERSION_MISMATCH;
        case VNPU_STATS_READER_IO_ERROR:
            return ENPU_RESOURCE_METRIC_STATS_UNAVAILABLE;
        case VNPU_STATS_READER_OK:
            break;
        default:
            return ENPU_RESOURCE_METRIC_STATS_UNAVAILABLE;
    }
    if (group->inconsistent_domain) {
        return ENPU_RESOURCE_METRIC_INCONSISTENT;
    }
    return group->missing_duration ? ENPU_RESOURCE_METRIC_AVG_DURATION_MISSING : ENPU_RESOURCE_METRIC_OK;
}

static void set_vnpu_metric(enpu_resource_metric_t *metric, const enpu_resource_metric_t *physical,
                            enpu_resource_metric_status_t stats_status, double weight, double work)
{
    metric->status = stats_status;
    if (stats_status != ENPU_RESOURCE_METRIC_OK) {
        return;
    }
    metric->status = physical->status;
    if (physical->status != ENPU_RESOURCE_METRIC_OK) {
        return;
    }
    metric->value = vnpu_utilization_share(physical->value, weight);
    metric->status = work > 0.0 ? ENPU_RESOURCE_METRIC_OK : ENPU_RESOURCE_METRIC_IDLE;
}

static void populate_hbm(enpu_resource_info_t *item, const resource_group_t *group, const allocation_t *source)
{
    enpu_resource_metric_status_t status = ENPU_RESOURCE_METRIC_UNAVAILABLE;
    if (item->vnpu_id >= VNPU_STATS_MAX_VNPU) {
        status = ENPU_RESOURCE_METRIC_VNPU_ID_OUT_OF_RANGE;
    } else if (group->reader_status != VNPU_STATS_READER_OK) {
        switch (group->reader_status) {
            case VNPU_STATS_READER_MISSING:
                status = ENPU_RESOURCE_METRIC_STATS_SHM_MISSING;
                break;
            case VNPU_STATS_READER_VERSION_MISMATCH:
                status = ENPU_RESOURCE_METRIC_STATS_VERSION_MISMATCH;
                break;
            default:
                status = ENPU_RESOURCE_METRIC_STATS_UNAVAILABLE;
                break;
        }
    } else {
        const vnpu_hbm_snapshot_t *hbm = &group->snapshot.hbm[item->vnpu_id];
        uint64_t now = group->snapshot.collected_at_ns;
        if (hbm->consistent && item->detail != NULL) {
            item->detail->hbm_source_result = hbm->source_error;
            item->detail->hbm_sampled_at_monotonic_ns = hbm->sampled_at_ns;
        }
        if (!hbm->consistent || group->ambiguous_chip) {
            status = ENPU_RESOURCE_METRIC_INCONSISTENT;
        } else if (hbm->status == VNPU_STATS_HBM_EMPTY || source->resource_generation == 0) {
            status = ENPU_RESOURCE_METRIC_UNAVAILABLE;
        } else if (hbm->generation != source->resource_generation || hbm->requested_generation != hbm->generation) {
            status = ENPU_RESOURCE_METRIC_INCONSISTENT;
        } else if (hbm->producer_instance == 0 || hbm->owner_epoch == 0) {
            status = ENPU_RESOURCE_METRIC_INCONSISTENT;
        } else if (hbm->sampled_at_ns > now || hbm->attempted_at_ns > now ||
                   hbm->attempted_at_ns < hbm->sampled_at_ns) {
            status = ENPU_RESOURCE_METRIC_INCONSISTENT;
        } else if ((hbm->status != VNPU_STATS_HBM_OK && hbm->status != VNPU_STATS_HBM_ERROR) ||
                   (hbm->status == VNPU_STATS_HBM_OK && hbm->source_error != 0) ||
                   (hbm->status == VNPU_STATS_HBM_ERROR && hbm->source_error == 0)) {
            status = ENPU_RESOURCE_METRIC_INCONSISTENT;
        } else if (hbm->status == VNPU_STATS_HBM_ERROR) {
            status = hbm->source_error == DCMI_ERR_CODE_NOT_SUPPORT ||
                             hbm->source_error == DCMI_ERR_CODE_NOT_SUPPORT_IN_CONTAINER ?
                         ENPU_RESOURCE_METRIC_UNSUPPORTED :
                         ENPU_RESOURCE_METRIC_UNAVAILABLE;
        } else if (hbm->sampled_at_ns == 0 || hbm->attempted_at_ns == 0 || now == 0) {
            status = ENPU_RESOURCE_METRIC_UNAVAILABLE;
        } else if (now - hbm->sampled_at_ns > VNPU_STATS_HBM_STALE_NS) {
            status = ENPU_RESOURCE_METRIC_STALE;
        } else {
            status = ENPU_RESOURCE_METRIC_OK;
            item->hbm.usage.used_bytes = hbm->used_bytes;
        }
        LOG_DEBUG("[RESOURCES] HBM snapshot: phy=%d vnpu=%d expected_gen=%llu actual_gen=%llu requested_gen=%llu "
                  "consistent=%d instance=%llu epoch=%llu status=%s ret=%d sampled_ns=%llu attempted_ns=%llu",
                  item->phy_id, item->vnpu_id, (unsigned long long)source->resource_generation,
                  (unsigned long long)hbm->generation, (unsigned long long)hbm->requested_generation, hbm->consistent,
                  (unsigned long long)hbm->producer_instance, (unsigned long long)hbm->owner_epoch,
                  enpu_resource_metric_status_string(status), hbm->source_error, (unsigned long long)hbm->sampled_at_ns,
                  (unsigned long long)hbm->attempted_at_ns);
    }
    item->hbm.usage.status = status;
}

static void populate_metrics(enpu_resource_info_t *item, const resource_group_t *group, const allocation_t *source,
                             const resource_physical_snapshot_t *physical)
{
    populate_hbm(item, group, source);
    enpu_resource_detail_t *detail = item->detail;
    set_physical_metric(&item->physical.npu_utilization, detail == NULL ? NULL : &detail->physical.npu_utilization,
                        &physical->metrics.npu, item->phy_id, "npu");
    set_physical_metric(&item->physical.aicore_utilization,
                        detail == NULL ? NULL : &detail->physical.aicore_utilization, &physical->metrics.aicore,
                        item->phy_id, "aicore");
    set_physical_metric(&item->physical.aicpu_utilization, detail == NULL ? NULL : &detail->physical.aicpu_utilization,
                        &physical->metrics.aicpu, item->phy_id, "aicpu");
    set_physical_metric(&item->physical.ctrlcpu_utilization,
                        detail == NULL ? NULL : &detail->physical.ctrlcpu_utilization, &physical->metrics.ctrlcpu,
                        item->phy_id, "ctrlcpu");
    set_physical_metric(&item->physical.cube_utilization, detail == NULL ? NULL : &detail->physical.cube_utilization,
                        &physical->metrics.cube, item->phy_id, "cube");
    set_physical_metric(&item->physical.vector_utilization,
                        detail == NULL ? NULL : &detail->physical.vector_utilization, &physical->metrics.vector,
                        item->phy_id, "vector");
    enpu_resource_metric_status_t status = work_status(group, item->vnpu_id);
    double work = 0.0;
    if (group->reader_status == VNPU_STATS_READER_OK && item->vnpu_id < VNPU_STATS_MAX_VNPU) {
        const vnpu_raw_metric_t *raw = &group->snapshot.metrics[item->vnpu_id];
        work = vnpu_utilization_work(raw->sum_block_dim, raw->avg_kernel_duration_ns);
        if (detail != NULL) {
            detail->sum_block_dim = raw->sum_block_dim;
            detail->launch_count = raw->launch_count;
            detail->avg_kernel_duration_ns = raw->avg_kernel_duration_ns;
            detail->work = work;
        }
    }
    double weight = status == ENPU_RESOURCE_METRIC_OK ? vnpu_utilization_weight(work, group->total_work) : 0.0;
    set_vnpu_metric(&item->vnpu.utilization, &item->physical.npu_utilization, status, weight, work);
    set_vnpu_metric(&item->vnpu.aicore_utilization, &item->physical.aicore_utilization, status, weight, work);
    if (detail != NULL) {
        detail->weight = weight;
        detail->vnpu.utilization.source_result = physical->metrics.npu.source_result;
        detail->vnpu.aicore_utilization.source_result = physical->metrics.aicore.source_result;
        detail->vnpu.utilization.sampled_at_monotonic_ns = group->snapshot.collected_at_ns;
        detail->vnpu.aicore_utilization.sampled_at_monotonic_ns = group->snapshot.collected_at_ns;
    }
    LOG_DEBUG("[RESOURCES] Result: allocation_id=%s phy=%d vnpu=%d shm=%s aicore_quota=%d policy=%d core_limit=%d "
              "hbm_request_bytes=%llu hbm_limit_bytes=%llu hbm_status=%s",
              item->allocation_id, item->phy_id, item->vnpu_id, group->source->shm_id, item->quota.aicore_percent,
              item->quota.scheduling_policy, item->quota.core_limit_enabled,
              (unsigned long long)item->hbm.request_bytes, (unsigned long long)item->hbm.limit_bytes,
              enpu_resource_metric_status_string(item->hbm.usage.status));
    LOG_DEBUG("[RESOURCES] Physical metrics: phy=%d vnpu=%d npu=%.1f(%s) aicore=%.1f(%s) aicpu=%.1f(%s)", item->phy_id,
              item->vnpu_id, item->physical.npu_utilization.value,
              enpu_resource_metric_status_string(item->physical.npu_utilization.status),
              item->physical.aicore_utilization.value,
              enpu_resource_metric_status_string(item->physical.aicore_utilization.status),
              item->physical.aicpu_utilization.value,
              enpu_resource_metric_status_string(item->physical.aicpu_utilization.status));
    LOG_DEBUG("[RESOURCES] Extended physical metrics: phy=%d vnpu=%d ctrlcpu=%.1f(%s) cube=%.1f(%s) vector=%.1f(%s)",
              item->phy_id, item->vnpu_id, item->physical.ctrlcpu_utilization.value,
              enpu_resource_metric_status_string(item->physical.ctrlcpu_utilization.status),
              item->physical.cube_utilization.value,
              enpu_resource_metric_status_string(item->physical.cube_utilization.status),
              item->physical.vector_utilization.value,
              enpu_resource_metric_status_string(item->physical.vector_utilization.status));
    LOG_DEBUG("[RESOURCES] vNPU attribution: phy=%d vnpu=%d work=%.3f total_work=%.3f weight=%.6f "
              "vnpu_util=%.3f(%s) vnpu_aicore=%.3f(%s)",
              item->phy_id, item->vnpu_id, work, group->total_work, weight, item->vnpu.utilization.value,
              enpu_resource_metric_status_string(item->vnpu.utilization.status), item->vnpu.aicore_utilization.value,
              enpu_resource_metric_status_string(item->vnpu.aicore_utilization.status));
}

int enpu_query_resources(enpu_manager_t *mgr, const enpu_resource_query_t *query, enpu_resource_response_t *response)
{
    if (response == NULL) {
        LOG_WARN("[RESOURCES] Invalid query arguments: response is NULL");
        return ENPU_INVALID_PARAM;
    }
    response->items = NULL;
    response->count = 0;
    if (mgr == NULL) {
        LOG_WARN("[RESOURCES] Invalid query arguments: manager is NULL");
        return ENPU_INVALID_PARAM;
    }
    int query_type = query == NULL ? ENPU_RESOURCE_QUERY_ALL : query->type;
    int phy_id = -1;
    int vnpu_id = -1;
    if (query_type == ENPU_RESOURCE_QUERY_BY_PHY) {
        phy_id = query->selector.phy_id;
    } else if (query_type == ENPU_RESOURCE_QUERY_BY_RESOURCE) {
        phy_id = query->selector.resource.phy_id;
        vnpu_id = query->selector.resource.vnpu_id;
    }
    int ret = validate_resource_query(query);
    if (ret != ENPU_SUCCESS) {
        LOG_WARN("[RESOURCES] Invalid query: type=%d phy=%d vnpu=%d", query_type, phy_id, vnpu_id);
        return ret;
    }
    const char *allocation_filter = query_type == ENPU_RESOURCE_QUERY_BY_ALLOCATION ? query->selector.allocation_id :
                                                                                      "";
    const char *pod_filter = query_type == ENPU_RESOURCE_QUERY_BY_METADATA ? query->selector.metadata.pod_uid : "";
    const char *container_filter =
        query_type == ENPU_RESOURCE_QUERY_BY_METADATA ? query->selector.metadata.container_name : "";
    uint64_t started_at_ns = resource_now_ns();
    LOG_DEBUG("[RESOURCES] Start query: type=%d phy=%d vnpu=%d allocation_id=%s pod=%s container=%s detail=%d",
              query_type, phy_id, vnpu_id, allocation_filter, pod_filter, container_filter,
              query != NULL && query->detail);
    allocation_t *allocations = calloc(MAX_ALLOC_COUNT, sizeof(*allocations));
    if (allocations == NULL) {
        LOG_ERROR("[RESOURCES] Failed to allocate allocation snapshot");
        return ENPU_NO_RESOURCE;
    }
    const allocation_t **sources = NULL;
    resource_group_t *groups = NULL;
    resource_physical_snapshot_t *physical_snapshots = NULL;
    int count = 0;
    const char *stage = "allocation_snapshot";
    allocation_query_t filter = {.phy_id = -1};
    ret = enpu_manager_query_allocations(mgr, &filter, allocations, &count);
    if (ret != ENPU_SUCCESS || count < 0 || count > MAX_ALLOC_COUNT) {
        LOG_ERROR("[RESOURCES] Allocation snapshot failed: ret=%d count=%d", ret, count);
        ret = ret == ENPU_SUCCESS ? ENPU_FAIL : ret;
        goto done;
    }
    if (count == 0) {
        LOG_DEBUG("[RESOURCES] Allocation snapshot empty: type=%d phy=%d vnpu=%d", query_type, phy_id, vnpu_id);
        ret = query_requires_match(query) ? ENPU_NOT_FOUND : ENPU_SUCCESS;
        goto done;
    }
    sources = calloc((size_t)count, sizeof(*sources));
    response->items = calloc((size_t)count, sizeof(*response->items));
    if (sources == NULL || response->items == NULL) {
        LOG_ERROR("[RESOURCES] Failed to allocate query buffers: allocations=%d item_bytes=%zu", count,
                  sizeof(*response->items));
        ret = ENPU_NO_RESOURCE;
        goto done;
    }
    uint64_t collected_at_ns = resource_now_ns();
    LOG_DEBUG("[RESOURCES] Allocation snapshot ready: allocations=%d", count);
    stage = "resource_identity_and_quota";
    for (int i = 0; i < count; ++i) {
        if (!matches_query(&allocations[i], query)) {
            continue;
        }
        size_t index = response->count;
        sources[index] = &allocations[i];
        enpu_resource_info_t *item = &response->items[response->count++];
        ret = initialize_resource(item, sources[index], query != NULL && query->detail, collected_at_ns);
        if (ret != ENPU_SUCCESS) {
            goto done;
        }
    }
    if (response->count == 0) {
        LOG_DEBUG("[RESOURCES] No resource matched: type=%d phy=%d vnpu=%d allocations=%d", query_type, phy_id, vnpu_id,
                  count);
        free(response->items);
        response->items = NULL;
        ret = query_requires_match(query) ? ENPU_NOT_FOUND : ENPU_SUCCESS;
        goto done;
    }
    groups = calloc(response->count, sizeof(*groups));
    if (groups == NULL) {
        LOG_ERROR("[RESOURCES] Failed to allocate groups: resources=%zu group_bytes=%zu", response->count,
                  sizeof(*groups));
        ret = ENPU_NO_RESOURCE;
        goto done;
    }
    physical_snapshots = calloc(response->count, sizeof(*physical_snapshots));
    if (physical_snapshots == NULL) {
        LOG_ERROR("[RESOURCES] Failed to allocate physical snapshots: resources=%zu", response->count);
        ret = ENPU_NO_RESOURCE;
        goto done;
    }
    size_t group_count = 0;
    size_t physical_count = 0;
    stage = "metric_snapshot";
    for (size_t i = 0; i < response->count; ++i) {
        size_t index = 0;
        for (; index < group_count; ++index) {
            if (strncmp(groups[index].source->shm_id, sources[i]->shm_id, SHM_ID_LEN) == 0) {
                break;
            }
        }
        if (index == group_count) {
            initialize_group(&groups[group_count++], sources[i], allocations, count);
        } else {
            LOG_DEBUG("[RESOURCES] Reuse stats snapshot: phy=%d vnpu=%d shm=%s", sources[i]->phy_id,
                      sources[i]->vnpu_id, sources[i]->shm_id);
        }
        size_t physical_index = 0;
        for (; physical_index < physical_count; ++physical_index) {
            if (physical_snapshots[physical_index].phy_id == sources[i]->phy_id) {
                break;
            }
        }
        if (physical_index == physical_count) {
            initialize_physical(&physical_snapshots[physical_count++], sources[i]->phy_id);
        } else {
            LOG_DEBUG("[RESOURCES] Reuse physical snapshot: phy=%d vnpu=%d", sources[i]->phy_id, sources[i]->vnpu_id);
        }
        populate_metrics(&response->items[i], &groups[index], sources[i], &physical_snapshots[physical_index]);
    }
    LOG_DEBUG("[RESOURCES] Query completed: resources=%zu groups=%zu physical_chips=%zu", response->count, group_count,
              physical_count);
    ret = ENPU_SUCCESS;
done:
    free(physical_snapshots);
    free(groups);
    free(sources);
    free(allocations);
    uint64_t finished_at_ns = resource_now_ns();
    LOG_DEBUG(
        "[RESOURCES] Query finished: type=%d phy=%d vnpu=%d allocation_id=%s pod=%s container=%s "
        "stage=%s ret=%d allocations=%zu elapsed_ns=%llu",
        query_type, phy_id, vnpu_id, allocation_filter, pod_filter, container_filter, stage, ret, response->count,
        (unsigned long long)(started_at_ns != 0 && finished_at_ns >= started_at_ns ? finished_at_ns - started_at_ns :
                                                                                     0));
    if (ret != ENPU_SUCCESS) {
        enpu_free_resource_response(response);
    }
    return ret;
}
