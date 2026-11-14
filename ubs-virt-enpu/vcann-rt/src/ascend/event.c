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
#include "rts_event.h"
#include "runtime_hook.h"
#include "stream_drain.h"

RUNTIME_HOOK_DEFINE(rtEventCreate, rtEvent_t *evt)
{
    LOG_DEBUG("Hook init rtEventCreate.");
    aclError ret = RUNTIME_HOOK_CALL(rt_library_entry, rtEventCreate, evt);
    if (ret == ACL_RT_SUCCESS && (is_core_limit() || get_sched_policy() == SCHED_POLICY_BEST_EFFORT)) {
        set_event_create_status((void *)(*evt));
    }
    return ret;
}

RUNTIME_HOOK_DEFINE(aclrtCreateEventImpl, aclrtEvent *event)
{
    if (!get_aclrt_impl_hook_enable()) {
        return RUNTIME_HOOK_CALL(rt_library_entry, aclrtCreateEventImpl, event);
    }
    LOG_DEBUG("Hook init aclrtCreateEventImpl.");
    aclError ret = RUNTIME_HOOK_CALL(rt_library_entry, aclrtCreateEventImpl, event);
    if (ret == ACL_RT_SUCCESS && (is_core_limit() || get_sched_policy() == SCHED_POLICY_BEST_EFFORT)) {
        set_event_create_status((void *)(*event));
    }
    return ret;
}

RUNTIME_HOOK_DEFINE(rtsEventCreate, rtEvent_t *evt, uint64_t flag)
{
    LOG_DEBUG("Hook init rtsEventCreate.");
    aclError ret = RUNTIME_HOOK_CALL(rt_library_entry, rtsEventCreate, evt, flag);
    if (ret == ACL_RT_SUCCESS && (is_core_limit() || get_sched_policy() == SCHED_POLICY_BEST_EFFORT)) {
        set_event_create_status((void *)(*evt));
    }
    return ret;
}

RUNTIME_HOOK_DEFINE(rtsEventCreateEx, rtEvent_t *evt, uint64_t flag)
{
    LOG_DEBUG("Hook init rtsEventCreateEx.");
    aclError ret = RUNTIME_HOOK_CALL(rt_library_entry, rtsEventCreateEx, evt, flag);
    if (ret == ACL_RT_SUCCESS && (is_core_limit() || get_sched_policy() == SCHED_POLICY_BEST_EFFORT)) {
        set_event_create_status((void *)(*evt));
    }
    return ret;
}

RUNTIME_HOOK_DEFINE(rtEventCreateWithFlag, rtEvent_t *evt, uint32_t flag)
{
    LOG_DEBUG("Hook init rtEventCreateWithFlag.");
    aclError ret = RUNTIME_HOOK_CALL(rt_library_entry, rtEventCreateWithFlag, evt, flag);
    if (ret == ACL_RT_SUCCESS && (is_core_limit() || get_sched_policy() == SCHED_POLICY_BEST_EFFORT)) {
        set_event_create_status((void *)(*evt));
    }
    return ret;
}

RUNTIME_HOOK_DEFINE(aclrtCreateEventWithFlagImpl, aclrtEvent *event, uint32_t flag)
{
    if (!get_aclrt_impl_hook_enable()) {
        return RUNTIME_HOOK_CALL(rt_library_entry, aclrtCreateEventWithFlagImpl, event, flag);
    }
    LOG_DEBUG("Hook init aclrtCreateEventWithFlagImpl.");
    aclError ret = RUNTIME_HOOK_CALL(rt_library_entry, aclrtCreateEventWithFlagImpl, event, flag);
    if (ret == ACL_RT_SUCCESS && (is_core_limit() || get_sched_policy() == SCHED_POLICY_BEST_EFFORT)) {
        set_event_create_status((void *)(*event));
    }
    return ret;
}

RUNTIME_HOOK_DEFINE(rtEventCreateExWithFlag, rtEvent_t *evt, uint32_t flag)
{
    LOG_DEBUG("Hook init rtEventCreateExWithFlag.");
    aclError ret = RUNTIME_HOOK_CALL(rt_library_entry, rtEventCreateExWithFlag, evt, flag);
    if (ret == ACL_RT_SUCCESS && (is_core_limit() || get_sched_policy() == SCHED_POLICY_BEST_EFFORT)) {
        set_event_create_status((void *)(*evt));
    }
    return ret;
}

RUNTIME_HOOK_DEFINE(aclrtCreateEventExWithFlagImpl, aclrtEvent *event, uint32_t flag)
{
    if (!get_aclrt_impl_hook_enable()) {
        return RUNTIME_HOOK_CALL(rt_library_entry, aclrtCreateEventExWithFlagImpl, event, flag);
    }
    LOG_DEBUG("Hook init aclrtCreateEventExWithFlagImpl.");
    aclError ret = RUNTIME_HOOK_CALL(rt_library_entry, aclrtCreateEventExWithFlagImpl, event, flag);
    if (ret == ACL_RT_SUCCESS && (is_core_limit() || get_sched_policy() == SCHED_POLICY_BEST_EFFORT)) {
        set_event_create_status((void *)(*event));
    }
    return ret;
}

RUNTIME_HOOK_DEFINE(rtStreamWaitEvent, rtStream_t stm, rtEvent_t evt)
{
    LOG_DEBUG("Hook init rtStreamWaitEvent.");
    aclError ret = RUNTIME_HOOK_CALL(rt_library_entry, rtStreamWaitEvent, stm, evt);
    if (ret == ACL_RT_SUCCESS && (is_core_limit() || get_sched_policy() == SCHED_POLICY_BEST_EFFORT)) {
        add_stream(stm);
        set_event_wait_status((void *)evt, stm);
    }
    return ret;
}

RUNTIME_HOOK_DEFINE(aclrtStreamWaitEventImpl, aclrtStream stream, aclrtEvent event)
{
    if (!get_aclrt_impl_hook_enable()) {
        return RUNTIME_HOOK_CALL(rt_library_entry, aclrtStreamWaitEventImpl, stream, event);
    }
    LOG_DEBUG("Hook init aclrtStreamWaitEventImpl.");
    aclError ret = RUNTIME_HOOK_CALL(rt_library_entry, aclrtStreamWaitEventImpl, stream, event);
    if (ret == ACL_RT_SUCCESS && (is_core_limit() || get_sched_policy() == SCHED_POLICY_BEST_EFFORT)) {
        add_stream(stream);
        set_event_wait_status((void *)event, stream);
    }
    return ret;
}

RUNTIME_HOOK_DEFINE(rtEventRecord, rtEvent_t evt, rtStream_t stm)
{
    LOG_DEBUG("Hook init rtEventRecord.");
    aclError ret = RUNTIME_HOOK_CALL(rt_library_entry, rtEventRecord, evt, stm);
    if (ret == ACL_RT_SUCCESS && (is_core_limit() || get_sched_policy() == SCHED_POLICY_BEST_EFFORT)) {
        add_stream(stm);
        set_event_record_status((void *)evt, stm);
    }
    return ret;
}

RUNTIME_HOOK_DEFINE(aclrtRecordEventImpl, aclrtEvent event, aclrtStream stream)
{
    if (!get_aclrt_impl_hook_enable()) {
        return RUNTIME_HOOK_CALL(rt_library_entry, aclrtRecordEventImpl, event, stream);
    }
    LOG_DEBUG("Hook init aclrtRecordEventImpl.");
    aclError ret = RUNTIME_HOOK_CALL(rt_library_entry, aclrtRecordEventImpl, event, stream);
    if (ret == ACL_RT_SUCCESS && (is_core_limit() || get_sched_policy() == SCHED_POLICY_BEST_EFFORT)) {
        add_stream(stream);
        set_event_record_status((void *)event, stream);
    }
    return ret;
}

RUNTIME_HOOK_DEFINE(aclrtRecordEventWithFlagImpl, aclrtEvent event, aclrtStream stream, uint32_t flag)
{
    if (!get_aclrt_impl_hook_enable()) {
        return RUNTIME_HOOK_CALL(rt_library_entry, aclrtRecordEventWithFlagImpl, event, stream, flag);
    }
    LOG_DEBUG("Hook init aclrtRecordEventWithFlagImpl.");
    aclError ret = RUNTIME_HOOK_CALL(rt_library_entry, aclrtRecordEventWithFlagImpl, event, stream, flag);
    if (ret == ACL_RT_SUCCESS && (is_core_limit() || get_sched_policy() == SCHED_POLICY_BEST_EFFORT)) {
        add_stream(stream);
        set_event_record_status((void *)event, stream);
    }
    return ret;
}

RUNTIME_HOOK_DEFINE(rtEventDestroy, rtEvent_t evt)
{
    LOG_DEBUG("Hook init rtEventDestroy.");
    aclError ret = RUNTIME_HOOK_CALL(rt_library_entry, rtEventDestroy, evt);
    if (ret == ACL_RT_SUCCESS && (is_core_limit() || get_sched_policy() == SCHED_POLICY_BEST_EFFORT)) {
        set_event_destroy_status((void *)evt);
    }
    return ret;
}

RUNTIME_HOOK_DEFINE(aclrtDestroyEventImpl, aclrtEvent event)
{
    if (!get_aclrt_impl_hook_enable()) {
        return RUNTIME_HOOK_CALL(rt_library_entry, aclrtDestroyEventImpl, event);
    }
    LOG_DEBUG("Hook init aclrtDestroyEventImpl.");
    aclError ret = RUNTIME_HOOK_CALL(rt_library_entry, aclrtDestroyEventImpl, event);
    if (ret == ACL_RT_SUCCESS && (is_core_limit() || get_sched_policy() == SCHED_POLICY_BEST_EFFORT)) {
        set_event_destroy_status((void *)event);
    }
    return ret;
}

RUNTIME_HOOK_DEFINE(rtStreamDestroy, rtStream_t stm)
{
    LOG_DEBUG("Hook init rtStreamDestroy.");
    core_limiter(stm, remove_stream, NULL);
    if (!stream_drain_wait_stream(stm)) {
        return ACL_ERROR_FAILURE;
    }
    aclError ret = RUNTIME_HOOK_CALL(rt_library_entry, rtStreamDestroy, stm);
    return ret;
}

RUNTIME_HOOK_DEFINE(aclrtDestroyStream, aclrtStream stream)
{
    LOG_DEBUG("Hook init aclrtDestroyStream.");
    core_limiter(stream, remove_stream, NULL);
    if (!stream_drain_wait_stream(stream)) {
        return ACL_ERROR_FAILURE;
    }
    aclError ret = RUNTIME_HOOK_CALL(rt_library_entry, aclrtDestroyStream, stream);
    return ret;
}

RUNTIME_HOOK_DEFINE(rtsNotifyCreate, rtNotify_t *notify, uint64_t flag)
{
    LOG_DEBUG("Hook init rtsNotifyCreate.");
    aclError ret = RUNTIME_HOOK_CALL(rt_library_entry, rtsNotifyCreate, notify, flag);
    if (ret == ACL_RT_SUCCESS && (is_core_limit() || get_sched_policy() == SCHED_POLICY_BEST_EFFORT)) {
        set_event_create_status((void *)(*notify));
    }
    return ret;
}

RUNTIME_HOOK_DEFINE(aclrtCreateNotifyImpl, aclrtNotify *notify, uint64_t flag)
{
    if (!get_aclrt_impl_hook_enable()) {
        return RUNTIME_HOOK_CALL(rt_library_entry, aclrtCreateNotifyImpl, notify, flag);
    }
    LOG_DEBUG("Hook init aclrtCreateNotifyImpl.");
    aclError ret = RUNTIME_HOOK_CALL(rt_library_entry, aclrtCreateNotifyImpl, notify, flag);
    if (ret == ACL_RT_SUCCESS && (is_core_limit() || get_sched_policy() == SCHED_POLICY_BEST_EFFORT)) {
        set_event_create_status((void *)(*notify));
    }
    return ret;
}

RUNTIME_HOOK_DEFINE(rtNotifyRecord, rtNotify_t notify, rtStream_t stm)
{
    LOG_DEBUG("Hook init rtNotifyRecord.");
    aclError ret = RUNTIME_HOOK_CALL(rt_library_entry, rtNotifyRecord, notify, stm);
    if (ret == ACL_RT_SUCCESS && (is_core_limit() || get_sched_policy() == SCHED_POLICY_BEST_EFFORT)) {
        add_stream(stm);
        set_event_record_status((void *)notify, stm);
    }
    return ret;
}

RUNTIME_HOOK_DEFINE(rtNotifyDestroy, rtNotify_t notify)
{
    LOG_DEBUG("Hook init rtNotifyDestroy.");
    aclError ret = RUNTIME_HOOK_CALL(rt_library_entry, rtNotifyDestroy, notify);
    if (ret == ACL_RT_SUCCESS && (is_core_limit() || get_sched_policy() == SCHED_POLICY_BEST_EFFORT)) {
        set_event_destroy_status((void *)notify);
    }
    return ret;
}

RUNTIME_HOOK_DEFINE(rtsNotifyWaitAndReset, rtNotify_t notify, rtStream_t stm, uint32_t timeout)
{
    LOG_DEBUG("Hook init rtsNotifyWaitAndReset.");
    aclError ret = RUNTIME_HOOK_CALL(rt_library_entry, rtsNotifyWaitAndReset, notify, stm, timeout);
    if (ret == ACL_RT_SUCCESS && (is_core_limit() || get_sched_policy() == SCHED_POLICY_BEST_EFFORT)) {
        add_stream(stm);
        set_event_wait_status((void *)notify, stm);
    }
    return ret;
}

RUNTIME_HOOK_DEFINE(aclrtWaitAndResetNotifyImpl, aclrtNotify notify, aclrtStream stream, uint32_t timeout)
{
    if (!get_aclrt_impl_hook_enable()) {
        return RUNTIME_HOOK_CALL(rt_library_entry, aclrtWaitAndResetNotifyImpl, notify, stream, timeout);
    }
    LOG_DEBUG("Hook init aclrtWaitAndResetNotifyImpl.");
    aclError ret = RUNTIME_HOOK_CALL(rt_library_entry, aclrtWaitAndResetNotifyImpl, notify, stream, timeout);
    if (ret == ACL_RT_SUCCESS && (is_core_limit() || get_sched_policy() == SCHED_POLICY_BEST_EFFORT)) {
        add_stream(stream);
        set_event_wait_status((void *)notify, stream);
    }
    return ret;
}

RUNTIME_HOOK_DEFINE(rtStreamWaitEventWithTimeout, rtStream_t stm, rtEvent_t evt, uint32_t timeout)
{
    LOG_DEBUG("Hook init rtStreamWaitEventWithTimeout.");
    aclError ret = RUNTIME_HOOK_CALL(rt_library_entry, rtStreamWaitEventWithTimeout, stm, evt, timeout);
    if (ret == ACL_RT_SUCCESS && (is_core_limit() || get_sched_policy() == SCHED_POLICY_BEST_EFFORT)) {
        add_stream(stm);
        set_event_wait_status((void *)evt, stm);
    }
    return ret;
}

RUNTIME_HOOK_DEFINE(rtEventDestroySync, rtEvent_t evt)
{
    LOG_DEBUG("Hook init rtEventDestroySync.");
    aclError ret = RUNTIME_HOOK_CALL(rt_library_entry, rtEventDestroySync, evt);
    if (ret == ACL_RT_SUCCESS && (is_core_limit() || get_sched_policy() == SCHED_POLICY_BEST_EFFORT)) {
        set_event_destroy_status((void *)evt);
    }
    return ret;
}

RUNTIME_HOOK_DEFINE(rtNotifyCreate, int32_t deviceId, rtNotify_t *notify)
{
    LOG_DEBUG("Hook init rtNotifyCreate.");
    aclError ret = RUNTIME_HOOK_CALL(rt_library_entry, rtNotifyCreate, deviceId, notify);
    if (ret == ACL_RT_SUCCESS && (is_core_limit() || get_sched_policy() == SCHED_POLICY_BEST_EFFORT)) {
        set_event_create_status((void *)(*notify));
    }
    return ret;
}

RUNTIME_HOOK_DEFINE(rtNotifyCreateWithFlag, int32_t deviceId, rtNotify_t *notify, uint32_t flag)
{
    LOG_DEBUG("Hook init rtNotifyCreateWithFlag.");
    aclError ret = RUNTIME_HOOK_CALL(rt_library_entry, rtNotifyCreateWithFlag, deviceId, notify, flag);
    if (ret == ACL_RT_SUCCESS && (is_core_limit() || get_sched_policy() == SCHED_POLICY_BEST_EFFORT)) {
        set_event_create_status((void *)(*notify));
    }
    return ret;
}

RUNTIME_HOOK_DEFINE(rtNotifyWait, rtNotify_t notify, rtStream_t stm)
{
    LOG_DEBUG("Hook init rtNotifyWait.");
    aclError ret = RUNTIME_HOOK_CALL(rt_library_entry, rtNotifyWait, notify, stm);
    if (ret == ACL_RT_SUCCESS && (is_core_limit() || get_sched_policy() == SCHED_POLICY_BEST_EFFORT)) {
        add_stream(stm);
        set_event_wait_status((void *)notify, stm);
    }
    return ret;
}

RUNTIME_HOOK_DEFINE(rtNotifyWaitWithTimeOut, rtNotify_t notify, rtStream_t stm, uint32_t timeOut)
{
    LOG_DEBUG("Hook init rtNotifyWaitWithTimeOut.");
    aclError ret = RUNTIME_HOOK_CALL(rt_library_entry, rtNotifyWaitWithTimeOut, notify, stm, timeOut);
    if (ret == ACL_RT_SUCCESS && (is_core_limit() || get_sched_policy() == SCHED_POLICY_BEST_EFFORT)) {
        add_stream(stm);
        set_event_wait_status((void *)notify, stm);
    }
    return ret;
}

RUNTIME_HOOK_DEFINE(rtCntNotifyCreate, const int32_t deviceId, rtCntNotify_t *const cntNotify)
{
    LOG_DEBUG("Hook init rtCntNotifyCreate.");
    aclError ret = RUNTIME_HOOK_CALL(rt_library_entry, rtCntNotifyCreate, deviceId, cntNotify);
    if (ret == ACL_RT_SUCCESS && (is_core_limit() || get_sched_policy() == SCHED_POLICY_BEST_EFFORT)) {
        set_event_create_status((void *)(*cntNotify));
    }
    return ret;
}

RUNTIME_HOOK_DEFINE(rtCntNotifyCreateWithFlag, const int32_t deviceId, rtCntNotify_t *const cntNotify,
                    const uint32_t flags)
{
    LOG_DEBUG("Hook init rtCntNotifyCreateWithFlag.");
    aclError ret = RUNTIME_HOOK_CALL(rt_library_entry, rtCntNotifyCreateWithFlag, deviceId, cntNotify, flags);
    if (ret == ACL_RT_SUCCESS && (is_core_limit() || get_sched_policy() == SCHED_POLICY_BEST_EFFORT)) {
        set_event_create_status((void *)(*cntNotify));
    }
    return ret;
}

RUNTIME_HOOK_DEFINE(aclrtCntNotifyCreateImpl, aclrtCntNotify *cntNotify, uint64_t flag)
{
    if (!get_aclrt_impl_hook_enable()) {
        return RUNTIME_HOOK_CALL(rt_library_entry, aclrtCntNotifyCreateImpl, cntNotify, flag);
    }
    LOG_DEBUG("Hook init aclrtCntNotifyCreateImpl.");
    aclError ret = RUNTIME_HOOK_CALL(rt_library_entry, aclrtCntNotifyCreateImpl, cntNotify, flag);
    if (ret == ACL_RT_SUCCESS && (is_core_limit() || get_sched_policy() == SCHED_POLICY_BEST_EFFORT)) {
        set_event_create_status((void *)(*cntNotify));
    }
    return ret;
}

RUNTIME_HOOK_DEFINE(rtCntNotifyRecord, rtCntNotify_t const inCntNotify, rtStream_t const stm,
                    const rtCntNtyRecordInfo_t *const info)
{
    LOG_DEBUG("Hook init rtCntNotifyRecord.");
    aclError ret = RUNTIME_HOOK_CALL(rt_library_entry, rtCntNotifyRecord, inCntNotify, stm, info);
    if (ret == ACL_RT_SUCCESS && (is_core_limit() || get_sched_policy() == SCHED_POLICY_BEST_EFFORT)) {
        add_stream(stm);
        set_event_record_status((void *)inCntNotify, stm);
    }
    return ret;
}

RUNTIME_HOOK_DEFINE(rtCntNotifyWaitWithTimeout, rtCntNotify_t const inCntNotify, rtStream_t const stm,
                    const rtCntNtyWaitInfo_t *const info)
{
    LOG_DEBUG("Hook init rtCntNotifyWaitWithTimeout.");
    aclError ret = RUNTIME_HOOK_CALL(rt_library_entry, rtCntNotifyWaitWithTimeout, inCntNotify, stm, info);
    if (ret == ACL_RT_SUCCESS && (is_core_limit() || get_sched_policy() == SCHED_POLICY_BEST_EFFORT)) {
        add_stream(stm);
        set_event_wait_status((void *)inCntNotify, stm);
    }
    return ret;
}

RUNTIME_HOOK_DEFINE(rtCntNotifyDestroy, rtCntNotify_t const inCntNotify)
{
    LOG_DEBUG("Hook init rtCntNotifyDestroy.");
    aclError ret = RUNTIME_HOOK_CALL(rt_library_entry, rtCntNotifyDestroy, inCntNotify);
    if (ret == ACL_RT_SUCCESS && (is_core_limit() || get_sched_policy() == SCHED_POLICY_BEST_EFFORT)) {
        set_event_destroy_status((void *)inCntNotify);
    }
    return ret;
}

RUNTIME_HOOK_DEFINE(aclrtCntNotifyDestroyImpl, aclrtCntNotify cntNotify)
{
    if (!get_aclrt_impl_hook_enable()) {
        return RUNTIME_HOOK_CALL(rt_library_entry, aclrtCntNotifyDestroyImpl, cntNotify);
    }
    LOG_DEBUG("Hook init aclrtCntNotifyDestroyImpl.");
    aclError ret = RUNTIME_HOOK_CALL(rt_library_entry, aclrtCntNotifyDestroyImpl, cntNotify);
    if (ret == ACL_RT_SUCCESS && (is_core_limit() || get_sched_policy() == SCHED_POLICY_BEST_EFFORT)) {
        set_event_destroy_status((void *)cntNotify);
    }
    return ret;
}

RUNTIME_HOOK_DEFINE(rtsCntNotifyRecord, rtCntNotify_t cntNotify, rtStream_t stm, rtCntNotifyRecordInfo_t *info)
{
    LOG_DEBUG("Hook init rtsCntNotifyRecord.");
    aclError ret = RUNTIME_HOOK_CALL(rt_library_entry, rtsCntNotifyRecord, cntNotify, stm, info);
    if (ret == ACL_RT_SUCCESS && (is_core_limit() || get_sched_policy() == SCHED_POLICY_BEST_EFFORT)) {
        add_stream(stm);
        set_event_record_status((void *)cntNotify, stm);
    }
    return ret;
}

RUNTIME_HOOK_DEFINE(aclrtCntNotifyRecordImpl, aclrtCntNotify cntNotify, aclrtStream stream,
                    aclrtCntNotifyRecordInfo *info)
{
    if (!get_aclrt_impl_hook_enable()) {
        return RUNTIME_HOOK_CALL(rt_library_entry, aclrtCntNotifyRecordImpl, cntNotify, stream, info);
    }
    LOG_DEBUG("Hook init aclrtCntNotifyRecordImpl.");
    aclError ret = RUNTIME_HOOK_CALL(rt_library_entry, aclrtCntNotifyRecordImpl, cntNotify, stream, info);
    if (ret == ACL_RT_SUCCESS && (is_core_limit() || get_sched_policy() == SCHED_POLICY_BEST_EFFORT)) {
        add_stream(stream);
        set_event_record_status((void *)cntNotify, stream);
    }
    return ret;
}

RUNTIME_HOOK_DEFINE(rtsCntNotifyWaitWithTimeout, rtCntNotify_t cntNotify, rtStream_t stm, rtCntNotifyWaitInfo_t *info)
{
    LOG_DEBUG("Hook init rtsCntNotifyWaitWithTimeout.");
    aclError ret = RUNTIME_HOOK_CALL(rt_library_entry, rtsCntNotifyWaitWithTimeout, cntNotify, stm, info);
    if (ret == ACL_RT_SUCCESS && (is_core_limit() || get_sched_policy() == SCHED_POLICY_BEST_EFFORT)) {
        add_stream(stm);
        set_event_wait_status((void *)cntNotify, stm);
    }
    return ret;
}

RUNTIME_HOOK_DEFINE(aclrtCntNotifyWaitWithTimeoutImpl, aclrtCntNotify cntNotify, aclrtStream stream,
                    aclrtCntNotifyWaitInfo *info)
{
    if (!get_aclrt_impl_hook_enable()) {
        return RUNTIME_HOOK_CALL(rt_library_entry, aclrtCntNotifyWaitWithTimeoutImpl, cntNotify, stream, info);
    }
    LOG_DEBUG("Hook init aclrtCntNotifyWaitWithTimeoutImpl.");
    aclError ret = RUNTIME_HOOK_CALL(rt_library_entry, aclrtCntNotifyWaitWithTimeoutImpl, cntNotify, stream, info);
    if (ret == ACL_RT_SUCCESS && (is_core_limit() || get_sched_policy() == SCHED_POLICY_BEST_EFFORT)) {
        add_stream(stream);
        set_event_wait_status((void *)cntNotify, stream);
    }
    return ret;
}
