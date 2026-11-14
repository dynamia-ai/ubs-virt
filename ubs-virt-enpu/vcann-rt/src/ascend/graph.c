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

#include "core_limiter.h"
#include "log.h"
#include "npu_manager.h"
#include "runtime_hook.h"
#include "stream_drain.h"
#include "vnpu_stats.h"

static void record_model_execute_stats(rtModel_t mdl)
{
    uint64_t block_dim = 0;
    uint64_t count = 0;
    if (model_stats_get(mdl, &block_dim, &count) == 0 && count > 0) {
        vnpu_stats_record(get_vnpu_id(), (uint32_t)block_dim, count);
    }
}

static void track_sync_model(void *param, rtStream_t stream)
{
    (void)stream;
    atomic_fetch_add(&hasModelExecuteSync, 1);
    *(bool *)param = true;
}

RUNTIME_HOOK_DEFINE(rtModelExecute, rtModel_t mdl, rtStream_t stm, uint32_t flag)
{
    LOG_DEBUG("Hook init rtModelExecute.");
    core_limiter(stm, NULL, NULL);
    record_model_execute_stats(mdl);
    if (is_random_sampling()) {
        sampling_begin(stm);
        aclError ret = RUNTIME_HOOK_CALL(rt_library_entry, rtModelExecute, mdl, stm, flag);
        sampling_graph_end(stm, mdl);
        return ret;
    }
    aclError ret = RUNTIME_HOOK_CALL(rt_library_entry, rtModelExecute, mdl, stm, flag);
    return ret;
}

RUNTIME_HOOK_DEFINE(aclmdlRIExecuteAsyncImpl, aclmdlRI modelRI, aclrtStream stream)
{
    if (!get_aclrt_impl_hook_enable()) {
        return RUNTIME_HOOK_CALL(rt_library_entry, aclmdlRIExecuteAsyncImpl, modelRI, stream);
    }
    LOG_DEBUG("Hook init aclmdlRIExecuteAsyncImpl.");
    core_limiter(stream, NULL, NULL);
    record_model_execute_stats(modelRI);
    if (is_random_sampling()) {
        sampling_begin(stream);
        aclError ret = RUNTIME_HOOK_CALL(rt_library_entry, aclmdlRIExecuteAsyncImpl, modelRI, stream);
        sampling_graph_end(stream, modelRI);
        return ret;
    }
    aclError ret = RUNTIME_HOOK_CALL(rt_library_entry, aclmdlRIExecuteAsyncImpl, modelRI, stream);
    return ret;
}

RUNTIME_HOOK_DEFINE(rtModelExecuteAsync, rtModel_t mdl, rtStream_t stm, uint32_t flag)
{
    LOG_DEBUG("Hook init rtModelExecuteAsync.");
    core_limiter(stm, NULL, NULL);
    record_model_execute_stats(mdl);
    if (is_random_sampling()) {
        sampling_begin(stm);
        aclError ret = RUNTIME_HOOK_CALL(rt_library_entry, rtModelExecuteAsync, mdl, stm, flag);
        sampling_graph_end(stm, mdl);
        return ret;
    }
    aclError ret = RUNTIME_HOOK_CALL(rt_library_entry, rtModelExecuteAsync, mdl, stm, flag);
    return ret;
}

RUNTIME_HOOK_DEFINE(rtsModelExecute, rtModel_t mdl, int32_t timeout)
{
    LOG_DEBUG("Hook init rtsModelExecute.");
    bool tracked = false;
    core_limiter(NULL, track_sync_model, &tracked);
    record_model_execute_stats(mdl);
    if (is_random_sampling()) {
        uint64_t begin_time = ns_now();
        aclError ret = RUNTIME_HOOK_CALL(rt_library_entry, rtsModelExecute, mdl, timeout);
        uint64_t end_time = ns_now();
        int rc = add_sample_record(get_kernel_count(mdl), end_time - begin_time, end_time);
        CHECK_ERROR_CODE(rc, "Add rtsModelExecute sample record failed.");
        if (tracked) {
            atomic_fetch_sub(&hasModelExecuteSync, 1);
        }
        return ret;
    }
    aclError ret = RUNTIME_HOOK_CALL(rt_library_entry, rtsModelExecute, mdl, timeout);
    if (tracked) {
        atomic_fetch_sub(&hasModelExecuteSync, 1);
    }
    return ret;
}

RUNTIME_HOOK_DEFINE(aclmdlRIExecuteImpl, aclmdlRI modelRI, int32_t timeout)
{
    if (!get_aclrt_impl_hook_enable()) {
        return RUNTIME_HOOK_CALL(rt_library_entry, aclmdlRIExecuteImpl, modelRI, timeout);
    }
    LOG_DEBUG("Hook init aclmdlRIExecuteImpl.");
    bool tracked = false;
    core_limiter(NULL, track_sync_model, &tracked);
    record_model_execute_stats(modelRI);
    if (is_random_sampling()) {
        uint64_t begin_time = ns_now();
        aclError ret = RUNTIME_HOOK_CALL(rt_library_entry, aclmdlRIExecuteImpl, modelRI, timeout);
        uint64_t end_time = ns_now();
        int rc = add_sample_record(get_kernel_count(modelRI), end_time - begin_time, end_time);
        CHECK_ERROR_CODE(rc, "Add aclmdlRIExecuteImpl sample record failed.");
        if (tracked) {
            atomic_fetch_sub(&hasModelExecuteSync, 1);
        }
        return ret;
    }
    aclError ret = RUNTIME_HOOK_CALL(rt_library_entry, aclmdlRIExecuteImpl, modelRI, timeout);
    if (tracked) {
        atomic_fetch_sub(&hasModelExecuteSync, 1);
    }
    return ret;
}

RUNTIME_HOOK_DEFINE(rtModelExecuteSync, rtModel_t mdl, rtStream_t stm, uint32_t flag, int32_t timeout)
{
    LOG_DEBUG("Hook init rtModelExecuteSync.");
    bool tracked = false;
    core_limiter(stm, track_sync_model, &tracked);
    record_model_execute_stats(mdl);
    if (is_random_sampling()) {
        sampling_begin(stm);
        aclError ret = RUNTIME_HOOK_CALL(rt_library_entry, rtModelExecuteSync, mdl, stm, flag, timeout);
        sampling_graph_end(stm, mdl);
        if (tracked) {
            atomic_fetch_sub(&hasModelExecuteSync, 1);
        }
        return ret;
    }
    aclError ret = RUNTIME_HOOK_CALL(rt_library_entry, rtModelExecuteSync, mdl, stm, flag, timeout);
    if (tracked) {
        atomic_fetch_sub(&hasModelExecuteSync, 1);
    }
    return ret;
}

RUNTIME_HOOK_DEFINE(rtStreamBeginCapture, rtStream_t stm, const rtStreamCaptureMode mode)
{
    LOG_DEBUG("Hook init rtStreamBeginCapture.");
    bool capture = true;
    core_limiter(stm, set_stream_capture, &capture);
    if (!stream_drain_wait_stream(stm)) {
        capture = false;
        set_stream_capture(&capture, stm);
        return ACL_ERROR_FAILURE;
    }
    aclError ret = RUNTIME_HOOK_CALL(rt_library_entry, rtStreamBeginCapture, stm, mode);
    if (ret != ACL_RT_SUCCESS) {
        capture = false;
        set_stream_capture(&capture, stm);
    }
    return ret;
}

RUNTIME_HOOK_DEFINE(aclmdlRICaptureBeginImpl, aclrtStream stream, aclmdlRICaptureMode mode)
{
    if (!get_aclrt_impl_hook_enable()) {
        return RUNTIME_HOOK_CALL(rt_library_entry, aclmdlRICaptureBeginImpl, stream, mode);
    }
    LOG_DEBUG("Hook init aclmdlRICaptureBeginImpl.");
    bool capture = true;
    core_limiter(stream, set_stream_capture, &capture);
    if (!stream_drain_wait_stream(stream)) {
        capture = false;
        set_stream_capture(&capture, stream);
        return ACL_ERROR_FAILURE;
    }
    aclError ret = RUNTIME_HOOK_CALL(rt_library_entry, aclmdlRICaptureBeginImpl, stream, mode);
    if (ret != ACL_RT_SUCCESS) {
        capture = false;
        set_stream_capture(&capture, stream);
    }
    return ret;
}

RUNTIME_HOOK_DEFINE(rtStreamEndCapture, rtStream_t stm, rtModel_t *captureMdl)
{
    LOG_DEBUG("Hook init rtStreamEndCapture.");
    core_limiter(stm, NULL, NULL);
    aclError ret = RUNTIME_HOOK_CALL(rt_library_entry, rtStreamEndCapture, stm, captureMdl);
    if (ret == ACL_RT_SUCCESS && (is_core_limit() || get_sched_policy() == SCHED_POLICY_BEST_EFFORT)) {
        bool capture = false;
        set_stream_capture(&capture, stm);
        if (captureMdl != NULL && *captureMdl != NULL) {
            int transfer_ret = capture_stats_transfer_to_model(stm, *captureMdl);
            if (transfer_ret != 0) {
                LOG_WARN("Failed to transfer capture stats to model for stream %p.", (void *)stm);
            }
        }
    }
    return ret;
}

RUNTIME_HOOK_DEFINE(aclmdlRICaptureEndImpl, aclrtStream stream, aclmdlRI *modelRI)
{
    if (!get_aclrt_impl_hook_enable()) {
        return RUNTIME_HOOK_CALL(rt_library_entry, aclmdlRICaptureEndImpl, stream, modelRI);
    }
    LOG_DEBUG("Hook init aclmdlRICaptureEndImpl.");
    core_limiter(stream, NULL, NULL);
    aclError ret = RUNTIME_HOOK_CALL(rt_library_entry, aclmdlRICaptureEndImpl, stream, modelRI);
    if (ret == ACL_RT_SUCCESS && (is_core_limit() || get_sched_policy() == SCHED_POLICY_BEST_EFFORT)) {
        bool capture = false;
        set_stream_capture(&capture, stream);
        if (modelRI != NULL && *modelRI != NULL) {
            int transfer_ret = capture_stats_transfer_to_model(stream, *modelRI);
            if (transfer_ret != 0) {
                LOG_WARN("Failed to transfer capture stats to model for stream %p.", (void *)stream);
            }
        }
    }
    return ret;
}

RUNTIME_HOOK_DEFINE(rtsModelExecuteAsync, rtModel_t mdl, rtStream_t stm)
{
    LOG_DEBUG("Hook init rtsModelExecuteAsync.");
    core_limiter(stm, NULL, NULL);
    record_model_execute_stats(mdl);
    if (is_random_sampling()) {
        sampling_begin(stm);
        aclError ret = RUNTIME_HOOK_CALL(rt_library_entry, rtsModelExecuteAsync, mdl, stm);
        sampling_graph_end(stm, mdl);
        return ret;
    }
    return RUNTIME_HOOK_CALL(rt_library_entry, rtsModelExecuteAsync, mdl, stm);
}
