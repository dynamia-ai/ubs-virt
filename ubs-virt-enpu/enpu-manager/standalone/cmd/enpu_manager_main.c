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

#include <securec.h>
#include <signal.h>
#include <stdatomic.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/un.h>
#include <unistd.h>
#include "enpu_config.h"
#include "enpu_manager.h"
#include "log.h"
#include "rest_api.h"

static atomic_bool g_running = true;
static enpu_manager_t *g_manager = NULL;
static rest_api_server_t *g_rest_server = NULL;
static bool g_manager_started = false;

static int print_usage(const char *prog_name)
{
    if (fprintf(stderr, "Usage: %s [OPTIONS]\n", prog_name) < 0) {
        return ENPU_FAIL;
    }
    if (fprintf(stderr, "Options:\n") < 0) {
        return ENPU_FAIL;
    }
    if (fprintf(stderr, "  --socket <path> HTTP over UDS path (default: %s)\n", ENPU_CONFIG_DEFAULT_SOCKET_PATH) < 0) {
        return ENPU_FAIL;
    }
    if (fprintf(stderr, "  --help          Show this help message\n") < 0) {
        return ENPU_FAIL;
    }
    return ENPU_SUCCESS;
}

static atomic_bool g_reload_requested = false;

static void reload_handler(int sig)
{
    (void)sig;
    atomic_store(&g_reload_requested, true);
}

static void signal_handler(int sig)
{
    (void)sig;
    atomic_store(&g_running, false);
}

static int setup_signal_handlers(void)
{
    struct sigaction sa;
    int ret = memset_s(&sa, sizeof(sa), 0, sizeof(sa));
    if (ret != 0) {
        CHECK_COND_LOG_PRINT(fprintf(stderr, "Failed to init sigaction: %d\n", ret) < 0, "print error message failed.");
        return -1;
    }
    sa.sa_handler = signal_handler;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = 0;

    if (sigaction(SIGINT, &sa, NULL) < 0) {
        perror("Failed to register SIGINT handler");
        return -1;
    }

    if (sigaction(SIGTERM, &sa, NULL) < 0) {
        perror("Failed to register SIGTERM handler");
        return -1;
    }

    sa.sa_handler = reload_handler;
    if (sigaction(SIGHUP, &sa, NULL) < 0) {
        perror("Failed to register SIGHUP handler");
        return -1;
    }

    return 0;
}

#define PARSE_ARG_HELP 1

static int parse_arguments(int argc, char *argv[], char *socket_path, size_t socket_path_size)
{
    int i = 1;
    while (i < argc) {
        if (strcmp(argv[i], "--help") == 0) {
            int usage_ret = print_usage(argv[0]);
            CHECK_COND_RETURN_(usage_ret != ENPU_SUCCESS, -1, "print usage failed.");
            return PARSE_ARG_HELP;
        } else if (strcmp(argv[i], "--socket") == 0) {
            if (i + 1 >= argc) {
                CHECK_COND_LOG_PRINT(fprintf(stderr, "Error: --socket requires a path argument\n") < 0,
                                     "print error message failed.");
                return -1;
            }
            i++;
            if (argv[i][0] != '/' || strnlen(argv[i], sizeof(((struct sockaddr_un *)0)->sun_path)) >=
                                         sizeof(((struct sockaddr_un *)0)->sun_path)) {
                CHECK_COND_LOG_PRINT(fprintf(stderr, "Error: Invalid Unix socket path\n") < 0,
                                     "print error message failed.");
                return -1;
            }
            int copy_ret = strncpy_s(socket_path, socket_path_size, argv[i], socket_path_size - 1);
            CHECK_COND_RETURN_(copy_ret != 0, -1, "copy socket path failed.");
        } else {
            CHECK_COND_LOG_PRINT(fprintf(stderr, "Error: Unknown option: %s\n", argv[i]) < 0,
                                 "print error message failed.");
            return -1;
        }
        i++;
    }

    return 0;
}

static void cleanup(void)
{
    if (g_rest_server) {
        rest_api_stop(g_rest_server);
        rest_api_destroy(g_rest_server);
        g_rest_server = NULL;
    }

    if (g_manager) {
        if (g_manager_started) {
            enpu_manager_stop(g_manager);
        }
        enpu_manager_destroy(g_manager);
        g_manager = NULL;
    }

    if (g_manager_started) {
        LOG_INFO("enpu-manager stopped");
    }
    log_shutdown();
}

static int build_enpu_manager_config(enpu_config_t *enpu_cfg, enpu_manager_config_t *mgr_cfg)
{
    int ret = snprintf_s(mgr_cfg->config_base_path, sizeof(mgr_cfg->config_base_path),
                         sizeof(mgr_cfg->config_base_path) - 1, "%s/vcann-rt", enpu_cfg->config_dir);
    CHECK_COND_RETURN_ERROR_CODE(ret < 0, "snprintf_s config_base_path failed.");
    ret = snprintf_s(mgr_cfg->checkpoint_path, sizeof(mgr_cfg->checkpoint_path), sizeof(mgr_cfg->checkpoint_path) - 1,
                     "%s/checkpoint.json", enpu_cfg->state_dir);
    CHECK_COND_RETURN_ERROR_CODE(ret < 0, "snprintf_s checkpoint_path failed.");
    ret = strncpy_s(mgr_cfg->evaluator_name, sizeof(mgr_cfg->evaluator_name), "share", sizeof("share") - 1);
    CHECK_COND_RETURN_ERROR_CODE(ret != 0, "strncpy_s evaluator_name failed.");
    ret = strncpy_s(mgr_cfg->share_strategy, sizeof(mgr_cfg->share_strategy), enpu_cfg->share_strategy,
                    sizeof(mgr_cfg->share_strategy) - 1);
    CHECK_COND_RETURN_ERROR_CODE(ret != 0, "strncpy_s share_strategy failed.");
    mgr_cfg->use_dcmi_stub = false;
    ret = memcpy_s(mgr_cfg->oversub_ratio, sizeof(mgr_cfg->oversub_ratio), enpu_cfg->oversub_ratio,
                   sizeof(enpu_cfg->oversub_ratio));
    CHECK_COND_RETURN_ERROR_CODE(ret != 0, "memcpy_s oversub_ratio failed.");
    mgr_cfg->swap_pre_watermark = enpu_cfg->swap_pre_watermark;

    return 0;
}

int main(int argc, char *argv[])
{
    char socket_path[MAX_PATH_LEN] = {0};
    int ret;
    int msg_ret = 0;
    enpu_config_t *enpu_cfg = NULL;
    enpu_manager_config_t mgr_cfg = {0};

    enpu_cfg = enpu_config_load(NULL);
    if (enpu_cfg == NULL) {
        msg_ret = fprintf(stderr, "[ERROR] Failed to load enpu config\n");
        CHECK_COND_LOG_PRINT(msg_ret < 0, "print error message failed.");
        return EXIT_FAILURE;
    }

    log_set_config(enpu_cfg->log_dir, enpu_cfg->log_level, (size_t)enpu_cfg->log_max_size * 1024ULL * 1024ULL,
                   enpu_cfg->log_max_backups, enpu_cfg->log_max_age);
    log_set_console_output(enpu_cfg->log_console != 0);
    if (log_init() != ENPU_SUCCESS) {
        msg_ret = fprintf(stderr, "[ERROR] Failed to init log module\n");
        CHECK_COND_LOG_PRINT(msg_ret < 0, "print error message failed.");
        enpu_config_destroy(enpu_cfg);
        return EXIT_FAILURE;
    }

    ret = strncpy_s(socket_path, sizeof(socket_path), enpu_cfg->socket_path, sizeof(socket_path) - 1);
    if (ret != 0) {
        enpu_config_destroy(enpu_cfg);
        return EXIT_FAILURE;
    }

    ret = parse_arguments(argc, argv, socket_path, sizeof(socket_path));
    if (ret == PARSE_ARG_HELP) {
        enpu_config_destroy(enpu_cfg);
        return EXIT_SUCCESS;
    }
    if (ret != 0) {
        enpu_config_destroy(enpu_cfg);
        return EXIT_FAILURE;
    }

    ret = setup_signal_handlers();
    if (ret != 0) {
        enpu_config_destroy(enpu_cfg);
        return EXIT_FAILURE;
    }

    LOG_INFO("Starting enpu-manager...");
    LOG_INFO("REST API Unix socket: %s", socket_path);
    {
        bool any_swap = false;
        for (int i = 0; i < MAX_NPU_PER_NODE; i++) {
            if (enpu_cfg->oversub_ratio[i] > 0.0) {
                any_swap = true;
                break;
            }
        }
        LOG_INFO("Swap (per-die oversub-ratio) enabled: %s", any_swap ? "true" : "false");
        if (any_swap) {
            LOG_INFO("  per-die oversub-ratio:");
            for (int i = 0; i < MAX_NPU_PER_NODE; i++) {
                if (enpu_cfg->oversub_ratio[i] > 0.0) {
                    LOG_INFO(" phy%d=%.2f", i, enpu_cfg->oversub_ratio[i]);
                }
            }
        }
    }

    ret = build_enpu_manager_config(enpu_cfg, &mgr_cfg);
    enpu_config_destroy(enpu_cfg);
    enpu_cfg = NULL;

    g_manager = enpu_manager_create(&mgr_cfg);
    if (g_manager == NULL) {
        LOG_ERROR("Failed to create enpu manager");
        return EXIT_FAILURE;
    }

    ret = enpu_manager_start(g_manager);
    if (ret != ENPU_SUCCESS) {
        LOG_ERROR("Failed to start enpu manager (watchdog/swap resolver): %d", ret);
        cleanup();
        return EXIT_FAILURE;
    }
    g_manager_started = true;

    g_rest_server = rest_api_create(socket_path);
    if (g_rest_server == NULL) {
        LOG_ERROR("Failed to create REST API server");
        cleanup();
        return EXIT_FAILURE;
    }

    rest_api_set_manager(g_rest_server, g_manager);

    ret = rest_api_start(g_rest_server);
    if (ret != 0) {
        LOG_ERROR("Failed to start REST API server: %d", ret);
        cleanup();
        return EXIT_FAILURE;
    }

    LOG_INFO("enpu-manager started successfully");
    LOG_INFO("REST API listening on Unix socket %s", socket_path);

    while (atomic_load(&g_running)) {
        if (atomic_exchange(&g_reload_requested, false)) {
            LOG_INFO("SIGHUP received: config reload is not supported yet, ignoring");
        }
        sleep(1);
    }

    LOG_INFO("Shutting down enpu-manager...");
    cleanup();

    return EXIT_SUCCESS;
}
