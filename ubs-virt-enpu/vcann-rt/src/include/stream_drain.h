/* Copyright (c) Huawei Technologies Co., Ltd. 2026. All rights reserved.
 * Licensed under Mulan PSL v2. */
#ifndef ENPU_STREAM_DRAIN_H
#define ENPU_STREAM_DRAIN_H
#include <runtime/rt.h>
#include <stdbool.h>
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif
int stream_drain_start(void);
/* One immutable batch per process; a wedged Runtime call never spawns replacements. */
bool stream_drain_submit(const rtStream_t *streams, int count, uint64_t turn);
bool stream_drain_finished(void);
bool stream_drain_succeeded(void);
rtStream_t stream_drain_current_stream(void);
/* A destroy must not race a worker using the same stream handle. */
bool stream_drain_uses(rtStream_t stream);
bool stream_drain_wait_stream(rtStream_t stream);
#ifdef __cplusplus
}
#endif
#endif
