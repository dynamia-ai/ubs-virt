/*
 * Copyright (c) Huawei Technologies Co., Ltd. 2026-2026. All rights reserved.
 * ubs-virt-enpu is licensed under Mulan PSL v2.
 * You can use this software according to the terms and conditions of the Mulan PSL v2.
 * You may obtain a copy of Mulan PSL v2 at:
 *          http://license.coscl.org.cn/MulanPSL2
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND,
 * EITHER EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT,
 * MERCHANTABILITY OR FIT FOR A PARTICULAR PURPOSE.
 * See the Mulan PSL v2 for more details.
 */
#ifndef _GNU_SOURCE
#define _GNU_SOURCE /* RTLD_NODELETE */
#endif
#include <dlfcn.h>
#include <errno.h>
#include <stdarg.h>
#include <string.h>
#include <unistd.h>
#include "common.h"
#include "core_limiter.h"
#include "dcmi_wrapper.h"
#include "npu_manager.h"
#include "runtime_hook.h"
#include "vnpu_metrics_reader.h"
#include "vnpu_utilization_calculator.h"

static bool g_diagnose = false;

static void die(const char *fmt, ...)
{
    va_list ap;

    va_start(ap, fmt);
    int ret = vfprintf(stderr, fmt, ap);
    va_end(ap);
    if (ret < 0) {
        LOG_ERROR("vfprintf failed.");
    }

    return;
}

static int parse_args(int argc, char *const argv[])
{
    if (argc == 2 && strcmp(argv[1], "--diagnose") == 0) {
        g_diagnose = true;
        return ENPU_SUCCESS;
    }
    if (argc > 1) {
        die("Invalid option : %s\nUsage: enpu-monitor [--diagnose]\n", argv[1]);
        return ENPU_FAIL;
    }
    return ENPU_SUCCESS;
}

static int diagnose_step(int result, const char *stage)
{
    if (g_diagnose && result != ENPU_SUCCESS) {
        die("[VNPU-STATS] Failed stage=%s ret=%d\n", stage, result);
    }
    return result;
}

static int load_rt_for_monitor(void)
{
    void *handle = dlopen("libruntime.so", RTLD_LAZY | RTLD_NODELETE);
    if (!handle) {
        LOG_ERROR("Failed to dlopen libruntime.so: %s", dlerror());
        return ENPU_FAIL;
    }

    for (int i = 0; i < RUNTIME_ENTRY_END; i++) {
        rt_library_entry[i].func_ptr = dlsym(handle, rt_library_entry[i].name);
        if (rt_library_entry[i].func_ptr == NULL) {
            LOG_DEBUG("Monitor: function %s not found, skipped.", rt_library_entry[i].name);
        }
    }
    dlclose(handle);
    return ENPU_SUCCESS;
}

typedef struct {
    int result;
    unsigned int value;
} monitor_physical_metric_t;

static void print_utilization(const char *label, int result, double value, const char *reason)
{
    if (result == ENPU_SUCCESS) {
        die("       %-30s : %.1f\n", label, value);
    } else {
        die("       %-30s : N/A (%s, ret=%d)\n", label, reason, result);
    }
}

static int monitor_npu_utilization(monitor_physical_metric_t *npu, monitor_physical_metric_t *aicore)
{
    if (is_core_limit()) {
        die("       Aicore Limit Quota(%%)     : %d\n", get_core_limit_quota());
    } else {
        die("       Aicore Limit Quota(%%)     : unlimited (best-effort)\n");
    }
    die("       Memory Request Quota(MB) : %zu\n"
        "       Memory Limit quota(MB)    : %zu\n",
        get_mem_request_quota() / MB_TO_B, get_mem_limit_quota() / MB_TO_B);

    uint64_t used = 0;
    int mem_ret = diagnose_step(
        enpu_dcmi_query_hbm_usage(get_logic_id(), get_card_id(), get_device_id(), get_soc_version(), &used),
        "memory_usage");
    if (mem_ret == ENPU_SUCCESS) {
        die("       Memory Usage(MB)          : %.3f\n"
            "       Memory Usage(Bytes)       : %llu\n",
            (double)used / MB_TO_B, (unsigned long long)used);
    } else {
        die("       Memory Usage(MB)          : N/A (DCMI unavailable, ret=%d)\n", mem_ret);
    }
    die("       Memory Usage Scope        : DCMI-visible processes; container-only scope not verified\n");

    const int types[] = {ENPU_DCMI_UTIL_NPU,     ENPU_DCMI_UTIL_AICORE, ENPU_DCMI_UTIL_AICPU,
                         ENPU_DCMI_UTIL_CTRLCPU, ENPU_DCMI_UTIL_CUBE,   ENPU_DCMI_UTIL_VECTOR};
    const char *labels[] = {"NPU Utilization(%)",         "NPU AICore Usage Rate(%)",  "NPU AI CPU Usage Rate(%)",
                            "NPU Ctrl CPU Usage Rate(%)", "NPU AI Cube Usage Rate(%)", "NPU AI Vector Usage Rate(%)"};
    for (size_t i = 0; i < sizeof(types) / sizeof(types[0]); ++i) {
        monitor_physical_metric_t metric = {ENPU_FAIL, 0};
        metric.result = enpu_dcmi_get_utilization_rate(get_logic_id(), get_card_id(), get_device_id(),
                                                       get_soc_version(), types[i], &metric.value);
        if (metric.result == ENPU_SUCCESS && metric.value > 100) {
            LOG_WARN("[VNPU-METRICS] Invalid physical utilization: type=%d value=%u.", types[i], metric.value);
            metric.result = ENPU_FAIL;
        }
        if (metric.result != ENPU_SUCCESS) {
            LOG_WARN("[VNPU-METRICS] Physical metric unavailable: type=%d ret=%d.", types[i], metric.result);
            (void)diagnose_step(metric.result, labels[i]);
        }
        print_utilization(labels[i], metric.result, (double)metric.value, "unsupported or query failed");
        if (types[i] == ENPU_DCMI_UTIL_NPU) {
            *npu = metric;
        } else if (types[i] == ENPU_DCMI_UTIL_AICORE) {
            *aicore = metric;
        }
    }
    return ENPU_SUCCESS;
}

static int monitor_vnpu_utilization(const monitor_physical_metric_t *npu, const monitor_physical_metric_t *aicore)
{
    uint8_t my_vnpu = get_vnpu_id();
    if (g_diagnose) {
        die("[VNPU-STATS] Query context: vnpu=%u shm=%s%s logic=%d card=%d device=%d policy=%d\n", my_vnpu,
            get_vnpu_shm_id(), VNPU_STATS_SHM_ID_SUFFIX, get_logic_id(), get_card_id(), get_device_id(),
            get_sched_policy());
        die("[VNPU-STATS] Read-only stats attachment; missing objects are not created.\n"
            "[VNPU-STATS] Query during sustained load in the same IPC/mount namespace as the workload.\n");
    }

    vnpu_stats_snapshot_t snapshot = {0};
    vnpu_stats_reader_status_t status = VNPU_STATS_READER_IO_ERROR;
    if (my_vnpu < VNPU_STATS_MAX_VNPU) {
        status = vnpu_metrics_reader_snapshot(get_vnpu_shm_id(), &snapshot);
    }
    int stats_ret = my_vnpu >= VNPU_STATS_MAX_VNPU               ? -ERANGE :
                    status == VNPU_STATS_READER_OK               ? ENPU_SUCCESS :
                    status == VNPU_STATS_READER_MISSING          ? -ENOENT :
                    status == VNPU_STATS_READER_VERSION_MISMATCH ? -EPROTO :
                                                                   -EIO;
    (void)diagnose_step(stats_ret, "attach_stats_shm");
    const char *reason = stats_ret == ENPU_SUCCESS ? "ok" :
                         stats_ret == -ENOENT      ? "stats_shm_missing" :
                         stats_ret == -EPROTO      ? "stats_version_mismatch" :
                         stats_ret == -ERANGE      ? "vnpu_id_out_of_range" :
                                                     "stats_unavailable";
    double numerator = 0.0;
    double denominator = 0.0;
    uint64_t my_blocks = 0;
    uint64_t my_avg_ns = 0;
    unsigned int missing_duration = 0;
    if (stats_ret == ENPU_SUCCESS) {
        for (int i = 0; i < VNPU_STATS_MAX_VNPU; ++i) {
            const vnpu_raw_metric_t *metric = &snapshot.metrics[i];
            uint64_t avg_duration = metric->avg_kernel_duration_ns;
            if (metric->sum_block_dim == 0ULL && !(g_diagnose && (uint8_t)i == my_vnpu)) {
                continue;
            }
            if ((uint8_t)i == my_vnpu) {
                my_blocks = metric->sum_block_dim;
                my_avg_ns = avg_duration;
            }
            if (g_diagnose) {
                die("[VNPU-STATS] Contributor: vnpu=%d blocks=%llu launches=%llu avg_ns=%llu\n", i,
                    (unsigned long long)metric->sum_block_dim, (unsigned long long)metric->launch_count,
                    (unsigned long long)avg_duration);
            }
            if (metric->sum_block_dim == 0ULL) {
                continue;
            }
            if (avg_duration == 0) {
                missing_duration++;
                continue;
            }
            double term = vnpu_utilization_work(metric->sum_block_dim, avg_duration);
            denominator += term;
            if ((uint8_t)i == my_vnpu) {
                numerator = term;
            }
        }
    }
    if (missing_duration != 0) {
        stats_ret = ENPU_FAIL;
        reason = "avg_duration_missing";
    }
    double weight = vnpu_utilization_weight(numerator, denominator);
    if (g_diagnose) {
        die("[VNPU-STATS] Attribution: blocks=%llu avg_ns=%llu work=%.3f total_work=%.3f weight=%.6f "
            "physical_npu=%u physical_aicore=%u missing_duration_contributors=%u status=%s\n",
            (unsigned long long)my_blocks, (unsigned long long)my_avg_ns, numerator, denominator, weight, npu->value,
            aicore->value, missing_duration, reason);
    }
    if (stats_ret != ENPU_SUCCESS) {
        print_utilization("vNPU Utilization(%)", stats_ret, 0.0, reason);
        print_utilization("vNPU AICore Usage Rate(%)", stats_ret, 0.0, reason);
    } else {
        print_utilization("vNPU Utilization(%)", npu->result, vnpu_utilization_share((double)npu->value, weight),
                          "physical_npu_unavailable");
        print_utilization("vNPU AICore Usage Rate(%)", aicore->result,
                          vnpu_utilization_share((double)aicore->value, weight), "physical_aicore_unavailable");
    }
    return ENPU_SUCCESS;
}

int main(int argc, char *argv[])
{
    int ret;

    ret = parse_args(argc, argv);
    if (ret != ENPU_SUCCESS) {
        return ret;
    }

    /* enpu-monitor 只包含业务输出（配额/使用率）*/
    g_log_silent = !g_diagnose;

    ret = diagnose_step(log_init(), "log_init");
    CHECK_RETURN_ERROR_CODE(ret, "Log init failed.");

    if (!g_diagnose && getenv("ENPU_LOG_LEVEL") != NULL) {
        unsetenv("ENPU_LOG_LEVEL");
    }

    ret = diagnose_step(load_rt_for_monitor(), "load_runtime");
    CHECK_RETURN_ERROR_CODE(ret, "Failed to load runtime library for monitor.");

    ret = diagnose_step(enpu_soc_init(), "soc_init");
    CHECK_RETURN_ERROR_CODE(ret, "Enpu soc init failed.");

    ret = diagnose_step(enpu_load_config(), "load_config");
    CHECK_RETURN_ERROR_CODE(ret, "Load npu device failed.");

    ret = diagnose_step(enpu_device_init(), "device_init");
    CHECK_RETURN_ERROR_CODE(ret, "Enpu device init failed.");

    monitor_physical_metric_t npu = {ENPU_FAIL, 0};
    monitor_physical_metric_t aicore = {ENPU_FAIL, 0};
    ret = monitor_npu_utilization(&npu, &aicore);
    CHECK_RETURN_ERROR_CODE(ret, "Npu utilization monitor failed.");

    ret = monitor_vnpu_utilization(&npu, &aicore);
    CHECK_RETURN_ERROR_CODE(ret, "vNPU utilization monitor failed.");

    if (g_diagnose) {
        log_shutdown();
    }
    return ENPU_SUCCESS;
}
