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
#include <fcntl.h>
#include <gtest/gtest.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/file.h>
#include <unistd.h>
#include <mockcpp/mockcpp.hpp>

#include "common.h"
#include "core_limiter.h"
#include "log.h"
#include "mem_limiter.h"
#include "npu_manager.h"
#include "runtime_hook.h"
#include "runtime_stub.h"
#include "securec.h"
#include "utils.h"
#include "vnpu_stats.h"

extern "C" {
// Non-static globals defined in vnpu_stats.c needed for state inspection.
extern SampleRecordNode *sample_record_head;
extern bool g_vnpu_stats_inited;     // test-visible on purpose, see vnpu_stats.c
extern bool g_sample_flag;           // in-flight sample marker owned by the sampling state machine
extern std::atomic<bool> g_sampling; // C atomic_bool, layout-compatible with std::atomic<bool>
extern rtEvent_t g_sample_start_event;
extern rtStream_t g_sample_stream;
}

// Swap one runtime hook table entry and restore the previous pointer on scope
// exit, so each test installs only the behaviors it needs on top of the
// default stubs wired by stub_load_rt_libraries.
class HookEntryGuard {
public:
    HookEntryGuard(rt_hook_enum_t id, void *stub) : id_(id), saved_(rt_library_entry[id].func_ptr)
    {
        rt_library_entry[id].func_ptr = stub;
    }

    ~HookEntryGuard()
    {
        rt_library_entry[id_].func_ptr = saved_;
    }

    HookEntryGuard(const HookEntryGuard &) = delete;
    HookEntryGuard &operator=(const HookEntryGuard &) = delete;

private:
    rt_hook_enum_t id_;
    void *saved_;
};

// ---- deterministic runtime hook stubs driving the sampling state machine ----

static rtEvent_t StubEventHandle()
{
    return reinterpret_cast<rtEvent_t>(0x1234);
}

static rtError_t StubCaptureNone(rtStream_t stm, rtStreamCaptureStatus *const status, rtModel_t *captureMdl)
{
    (void)stm;
    (void)captureMdl;
    if (status != NULL) {
        *status = RT_STREAM_CAPTURE_STATUS_NONE;
    }
    return RT_ERROR_NONE;
}

static rtError_t StubCaptureActive(rtStream_t stm, rtStreamCaptureStatus *const status, rtModel_t *captureMdl)
{
    (void)stm;
    (void)captureMdl;
    if (status != NULL) {
        *status = RT_STREAM_CAPTURE_STATUS_ACTIVE;
    }
    return RT_ERROR_NONE;
}

static rtError_t StubCaptureFail(rtStream_t stm, rtStreamCaptureStatus *const status, rtModel_t *captureMdl)
{
    (void)stm;
    (void)status;
    (void)captureMdl;
    return (rtError_t)0x111;
}

static rtError_t StubEventCreateOk(rtEvent_t *evt, uint32_t flag)
{
    (void)flag;
    if (evt != NULL) {
        *evt = StubEventHandle();
    }
    return RT_ERROR_NONE;
}

static rtError_t StubEventCreateFail(rtEvent_t *evt, uint32_t flag)
{
    (void)evt;
    (void)flag;
    return (rtError_t)0x222;
}

// 违约的被调方：返回失败却仍往出参写了值。sampling_begin 必须把全局量清掉，
// 否则里面会留下一个来源不明的句柄。
static rtError_t StubEventCreateFailButWrites(rtEvent_t *evt, uint32_t flag)
{
    (void)flag;
    if (evt != NULL) {
        *evt = StubEventHandle();
    }
    return (rtError_t)0x222;
}

static rtError_t StubEventRecordOk(rtEvent_t evt, rtStream_t stm)
{
    (void)evt;
    (void)stm;
    return RT_ERROR_NONE;
}

static rtError_t StubEventRecordFail(rtEvent_t evt, rtStream_t stm)
{
    (void)evt;
    (void)stm;
    return (rtError_t)0x333;
}

static rtError_t StubEventDestroyOk(rtEvent_t evt)
{
    (void)evt;
    return RT_ERROR_NONE;
}

static rtError_t StubEventElapsed2ms(float *timeInterval, rtEvent_t startEvent, rtEvent_t endEvent)
{
    (void)startEvent;
    (void)endEvent;
    if (timeInterval != NULL) {
        *timeInterval = 2.0f;
    }
    return RT_ERROR_NONE;
}

static rtError_t StubEventElapsed1ms(float *timeInterval, rtEvent_t startEvent, rtEvent_t endEvent)
{
    (void)startEvent;
    (void)endEvent;
    if (timeInterval != NULL) {
        *timeInterval = 1.0f;
    }
    return RT_ERROR_NONE;
}

static rtError_t StubStreamSyncOk(rtStream_t stm)
{
    (void)stm;
    return RT_ERROR_NONE;
}

// Model introspection used by get_kernel_count: sizing call has streams==NULL.
static rtError_t StubModelStreamsTwo(rtModel_t mdl, rtStream_t *streams, uint32_t *numStreams)
{
    (void)mdl;
    if (streams == NULL) {
        if (numStreams != NULL) {
            *numStreams = 2;
        }
        return RT_ERROR_NONE;
    }
    streams[0] = reinterpret_cast<rtStream_t>(0x10);
    streams[1] = reinterpret_cast<rtStream_t>(0x11);
    return RT_ERROR_NONE;
}

static rtError_t StubModelStreamsOne(rtModel_t mdl, rtStream_t *streams, uint32_t *numStreams)
{
    (void)mdl;
    if (streams == NULL) {
        if (numStreams != NULL) {
            *numStreams = 1;
        }
        return RT_ERROR_NONE;
    }
    streams[0] = reinterpret_cast<rtStream_t>(0x10);
    return RT_ERROR_NONE;
}

static rtError_t StubModelStreamsZeroNum(rtModel_t mdl, rtStream_t *streams, uint32_t *numStreams)
{
    (void)mdl;
    (void)streams;
    if (numStreams != NULL) {
        *numStreams = 0;
    }
    return RT_ERROR_NONE;
}

static rtError_t StubModelStreamsFailAlways(rtModel_t mdl, rtStream_t *streams, uint32_t *numStreams)
{
    (void)mdl;
    (void)streams;
    (void)numStreams;
    return (rtError_t)0x444;
}

static rtError_t StubModelStreamsFailSecond(rtModel_t mdl, rtStream_t *streams, uint32_t *numStreams)
{
    (void)mdl;
    if (streams == NULL) {
        if (numStreams != NULL) {
            *numStreams = 1;
        }
        return RT_ERROR_NONE;
    }
    return (rtError_t)0x555;
}

static rtError_t StubStreamTasksThreeOrFour(rtStream_t const stm, rtTask_t *tasks, uint32_t *numTasks)
{
    (void)tasks;
    if (numTasks != NULL) {
        *numTasks = (stm == reinterpret_cast<rtStream_t>(0x10)) ? 3U : 4U;
    }
    return RT_ERROR_NONE;
}

static rtError_t StubStreamTasksFail(rtStream_t const stm, rtTask_t *tasks, uint32_t *numTasks)
{
    (void)stm;
    (void)tasks;
    (void)numTasks;
    return (rtError_t)0x666;
}

static rtError_t StubStreamTasksZero(rtStream_t const stm, rtTask_t *tasks, uint32_t *numTasks)
{
    (void)stm;
    (void)tasks;
    if (numTasks != NULL) {
        *numTasks = 0;
    }
    return RT_ERROR_NONE;
}

// Poll a plain flag written by the detached sample_sync thread until it clears.
static bool WaitUntilFlagCleared(volatile const bool *flag, uint64_t timeoutNs)
{
    uint64_t deadline = ns_now() + timeoutNs;
    while (*flag) {
        if (ns_now() > deadline) {
            return false;
        }
        usleep(200);
    }
    return true;
}

// Release the whole sample list (nodes + head) so a forced re-init allocates a fresh head.
static void FreeSampleRecords()
{
    if (sample_record_head == NULL) {
        return;
    }
    SampleRecordNode *cur = sample_record_head->next;
    while (cur != NULL) {
        SampleRecordNode *next = cur->next;
        free(cur);
        cur = next;
    }
    free(sample_record_head);
    sample_record_head = NULL;
}

class VnpuStatsTest : public testing::Test {
protected:
    static void SetUpTestCase()
    {
        std::cout << "Vnpu stats test start" << std::endl;
    }

    static void TearDownTestCase()
    {
        std::cout << "Vnpu stats test end" << std::endl;
    }

    void SetUp() override
    {
        (void)sprintf_s(g_log_config.log_dir, sizeof(g_log_config.log_dir), "%s", "../__build/log/enpu/");
        fd_ = open(stub_lock_path(), O_CREAT | O_RDONLY, 0755);
        MOCKER(lock_path).stubs().will(invoke(stub_lock_path));
        MOCKER(enpu_load_config).stubs().will(invoke(stub_enpu_load_config));
        enpu_global_init();
        MOCKER(load_rt_libraries).stubs().will(invoke(stub_load_rt_libraries));
        ASSERT_EQ(stub_enpu_load_config(), ENPU_SUCCESS);

        // Remap the stats shm exactly like the enpu-monitor process does, so
        // bucket/slot eviction logic can be driven by direct field updates.
        char stats_shm_id[SHM_ID_LEN + 8] = {0};
        (void)snprintf(stats_shm_id, sizeof(stats_shm_id), "%s%s", get_vnpu_shm_id(), VNPU_STATS_SHM_ID_SUFFIX);
        shm_ = (vnpu_stats_shm_t *)map_share_mem(stats_shm_id, sizeof(vnpu_stats_shm_t));
        ASSERT_NE(shm_, nullptr);
        ResetStatsShm();
    }

    void TearDown() override
    {
        // Free any nodes left behind by list-based cases (same-process heap).
        if (sample_record_head != NULL) {
            while (sample_record_head->next != NULL) {
                SampleRecordNode *old = sample_record_head->next;
                sample_record_head->next = old->next;
                free(old);
            }
        }
        if (fd_ >= 0) {
            close(fd_);
            fd_ = -1;
        }
        GlobalMockObject::verify();
        GlobalMockObject::reset();
    }

    // Clear per-case writable state (buckets + avg slots), keep magic intact so
    // subsequent cases observe a clean but initialized shm.
    void ResetStatsShm()
    {
        for (int i = 0; i < MAX_VNPU; ++i) {
            for (int j = 0; j < VNPU_STATS_NUM_BUCKETS; ++j) {
                atomic_store(&shm_->buckets[i][j].bucket_start_ns, 0);
                atomic_store(&shm_->buckets[i][j].sum_block_dim, 0);
                atomic_store(&shm_->buckets[i][j].launch_count, 0);
            }
            atomic_store(&shm_->avg_slot_seq[i], 0);
            for (int s = 0; s < VNPU_STATS_AVG_SLOTS; ++s) {
                atomic_store(&shm_->avg_slots[i][s].sum_ns, 0);
                atomic_store(&shm_->avg_slots[i][s].count, 0);
                atomic_store(&shm_->avg_slots[i][s].ts_ns, 0);
            }
        }
    }

    SampleRecordNode *MakeRecord(int kernel_count, uint64_t sample_time, uint64_t timestamp)
    {
        SampleRecordNode *node = (SampleRecordNode *)malloc(sizeof(SampleRecordNode));
        EXPECT_NE(node, nullptr);
        node->kernel_count = kernel_count;
        node->sample_time = sample_time;
        node->timestamp = timestamp;
        node->next = NULL;
        return node;
    }

    vnpu_stats_shm_t *shm_ = nullptr;

private:
    int fd_ = -1;
};

// ---- vnpu_stats_query ----

// vnpu_stats_query rejects a null output pointer.
TEST_F(VnpuStatsTest, vnpu_stats_query_null_out_fails)
{
    EXPECT_EQ(vnpu_stats_query(get_vnpu_id(), NULL), ENPU_FAIL);
}

// vnpu_stats_query rejects an out-of-range vnpu_id.
TEST_F(VnpuStatsTest, vnpu_stats_query_invalid_vnpu_id_fails)
{
    vnpu_stats_aggregate_t agg;
    EXPECT_EQ(vnpu_stats_query(MAX_VNPU, &agg), ENPU_FAIL);
    EXPECT_EQ(vnpu_stats_query(MAX_VNPU + 1, &agg), ENPU_FAIL);
}

// vnpu_stats_query on a vnpu with no records returns zeros with success.
TEST_F(VnpuStatsTest, vnpu_stats_query_empty_vnpu_returns_zero)
{
    vnpu_stats_aggregate_t agg;
    EXPECT_EQ(vnpu_stats_query(get_vnpu_id(), &agg), ENPU_SUCCESS);
    EXPECT_EQ(agg.sum_block_dim, 0U);
    EXPECT_EQ(agg.launch_count, 0U);
}

// vnpu_stats_record accumulates block_dim/count in the current bucket and
// vnpu_stats_query aggregates them back within the window.
TEST_F(VnpuStatsTest, vnpu_stats_record_then_query_aggregates)
{
    uint8_t id = get_vnpu_id();
    vnpu_stats_record(id, 32, 1);
    vnpu_stats_record(id, 32, 1);
    vnpu_stats_record(id, 16, 2);

    vnpu_stats_aggregate_t agg;
    EXPECT_EQ(vnpu_stats_query(id, &agg), ENPU_SUCCESS);
    // sum: 32 + 32 + 16 = 80; count: 1 + 1 + 2 = 4.
    EXPECT_EQ(agg.sum_block_dim, 80U);
    EXPECT_EQ(agg.launch_count, 4U);
}

// vnpu_stats_record with an out-of-range vnpu_id is a safe no-op.
TEST_F(VnpuStatsTest, vnpu_stats_record_invalid_vnpu_id_noop)
{
    vnpu_stats_record(MAX_VNPU, 32, 1);
    vnpu_stats_record(MAX_VNPU + 1, 32, 1);
    vnpu_stats_aggregate_t agg;
    EXPECT_EQ(vnpu_stats_query(get_vnpu_id(), &agg), ENPU_SUCCESS);
    EXPECT_EQ(agg.sum_block_dim, 0U);
}

// vnpu_stats_query excludes buckets whose start falls outside the 1s window.
TEST_F(VnpuStatsTest, vnpu_stats_query_excludes_stale_bucket)
{
    uint8_t id = get_vnpu_id();
    vnpu_stats_record(id, 32, 1);

    // Force every written bucket out of the window (2s old).
    uint64_t now = ns_now();
    for (int j = 0; j < VNPU_STATS_NUM_BUCKETS; ++j) {
        uint64_t bts = atomic_load(&shm_->buckets[id][j].bucket_start_ns);
        if (bts != 0) {
            atomic_store(&shm_->buckets[id][j].bucket_start_ns, now - 2ULL * VNPU_STATS_WINDOW_NS);
        }
    }

    vnpu_stats_aggregate_t agg;
    EXPECT_EQ(vnpu_stats_query(id, &agg), ENPU_SUCCESS);
    EXPECT_EQ(agg.sum_block_dim, 0U);
    EXPECT_EQ(agg.launch_count, 0U);
}

// ---- vnpu_stats_set/get_local_avg_duration ----

// set followed by get returns the stored average for the slot.
TEST_F(VnpuStatsTest, vnpu_stats_set_then_get_avg_duration)
{
    uint8_t id = get_vnpu_id();
    vnpu_stats_set_local_avg_duration(id, 2ULL * VNPU_STATS_NS_PER_MS, 3);
    EXPECT_EQ(vnpu_stats_get_avg_duration_ns(id), (int64_t)(2ULL * VNPU_STATS_NS_PER_MS));
}

// set with count == 0 is ignored (defensive branch), get still returns 0.
TEST_F(VnpuStatsTest, vnpu_stats_set_zero_count_ignored)
{
    uint8_t id = get_vnpu_id();
    vnpu_stats_set_local_avg_duration(id, 5ULL * VNPU_STATS_NS_PER_MS, 0);
    EXPECT_EQ(vnpu_stats_get_avg_duration_ns(id), 0);
}

// get with an out-of-range vnpu_id returns 0.
TEST_F(VnpuStatsTest, vnpu_stats_get_avg_duration_invalid_vnpu_id)
{
    EXPECT_EQ(vnpu_stats_get_avg_duration_ns(MAX_VNPU), 0);
    EXPECT_EQ(vnpu_stats_get_avg_duration_ns(MAX_VNPU + 1), 0);
}

// get merges multiple live slots by weighted average: this is the
// multi-process semantics (each writer process owns one slot).
// slot0: avg 3ms * count 2, slot1: avg 9ms * count 2 => (6ms + 18ms) / 4 = 6ms.
TEST_F(VnpuStatsTest, vnpu_stats_get_avg_duration_multi_slot_weighted)
{
    uint8_t id = get_vnpu_id();
    uint64_t now = ns_now();
    atomic_store(&shm_->avg_slots[id][0].sum_ns, 3ULL * VNPU_STATS_NS_PER_MS * 2);
    atomic_store(&shm_->avg_slots[id][0].count, 2);
    atomic_store(&shm_->avg_slots[id][0].ts_ns, now);
    atomic_store(&shm_->avg_slots[id][1].sum_ns, 9ULL * VNPU_STATS_NS_PER_MS * 2);
    atomic_store(&shm_->avg_slots[id][1].count, 2);
    atomic_store(&shm_->avg_slots[id][1].ts_ns, now);

    EXPECT_EQ(vnpu_stats_get_avg_duration_ns(id), (int64_t)(6ULL * VNPU_STATS_NS_PER_MS));
}

// get ignores slots older than VNPU_STATS_AVG_STALE_NS (3 * window).
TEST_F(VnpuStatsTest, vnpu_stats_get_avg_duration_excludes_stale_slot)
{
    uint8_t id = get_vnpu_id();
    atomic_store(&shm_->avg_slots[id][0].sum_ns, 4ULL * VNPU_STATS_NS_PER_MS);
    atomic_store(&shm_->avg_slots[id][0].count, 1);
    atomic_store(&shm_->avg_slots[id][0].ts_ns, ns_now() - 4ULL * VNPU_STATS_WINDOW_NS);

    EXPECT_EQ(vnpu_stats_get_avg_duration_ns(id), 0);
}

// get ignores slots with zero timestamp or zero count.
TEST_F(VnpuStatsTest, vnpu_stats_get_avg_duration_ignores_zero_ts_and_count)
{
    uint8_t id = get_vnpu_id();
    // Zero ts (never written).
    atomic_store(&shm_->avg_slots[id][0].sum_ns, 1ULL * VNPU_STATS_NS_PER_MS);
    atomic_store(&shm_->avg_slots[id][0].count, 1);
    atomic_store(&shm_->avg_slots[id][0].ts_ns, 0);
    // Zero count.
    atomic_store(&shm_->avg_slots[id][1].sum_ns, 2ULL * VNPU_STATS_NS_PER_MS);
    atomic_store(&shm_->avg_slots[id][1].count, 0);
    atomic_store(&shm_->avg_slots[id][1].ts_ns, ns_now());

    EXPECT_EQ(vnpu_stats_get_avg_duration_ns(id), 0);
}

// ---- get_ker_ave_exec_time (defensive paths we added) ----

// get_ker_ave_exec_time returns 0 when the record head is not yet allocated
// (scheduler thread racing vnpu_stats_init regression guard).
TEST_F(VnpuStatsTest, get_ker_ave_exec_time_null_head_returns_zero)
{
    SampleRecordNode *saved = sample_record_head;
    sample_record_head = NULL;
    uint64_t count = 123;
    EXPECT_EQ(get_ker_ave_exec_time(&count), 0U);
    EXPECT_EQ(count, 0U);
    sample_record_head = saved;
}

// get_ker_ave_exec_time on an empty list (head only) returns 0.
TEST_F(VnpuStatsTest, get_ker_ave_exec_time_empty_list_returns_zero)
{
    sample_record_head->next = NULL;
    uint64_t count = 0;
    EXPECT_EQ(get_ker_ave_exec_time(&count), 0U);
    EXPECT_EQ(count, 0U);
}

// get_ker_ave_exec_time sums fresh records (sum_time / sum_count) and evicts
// the expired tail: [fresh(2,3ms), fresh(4,9ms), expired] => 12ms / 6 = 2ms.
TEST_F(VnpuStatsTest, get_ker_ave_exec_time_aggregates_and_evicts_expired)
{
    uint64_t now = ns_now();
    SampleRecordNode *n1 = MakeRecord(2, 3ULL * VNPU_STATS_NS_PER_MS, now);
    SampleRecordNode *n2 = MakeRecord(4, 9ULL * VNPU_STATS_NS_PER_MS, now);
    SampleRecordNode *old =
        MakeRecord(100, 1ULL * VNPU_STATS_NS_PER_MS, now - VNPU_STATS_WINDOW_NS - VNPU_STATS_NS_PER_MS);
    n1->next = n2;
    n2->next = old;
    sample_record_head->next = n1;

    uint64_t count = 0;
    EXPECT_EQ(get_ker_ave_exec_time(&count), 2ULL * VNPU_STATS_NS_PER_MS);
    EXPECT_EQ(count, 6U);

    // The expired node was freed and unlinked by remove_sample_records.
    // n1/n2 stay linked and are freed by the TearDown walker.
    EXPECT_EQ(sample_record_head->next, n1);
    EXPECT_EQ(n1->next, n2);
    EXPECT_EQ(n2->next, nullptr);

    remove_sample_records(sample_record_head);
}

TEST_F(VnpuStatsTest, add_sample_record_head_null)
{
    SampleRecordNode *saved = sample_record_head;
    sample_record_head = NULL;
    EXPECT_EQ(add_sample_record(1, 1ULL * VNPU_STATS_NS_PER_MS, ns_now()), ENPU_FAIL);
    sample_record_head = saved;
}

TEST_F(VnpuStatsTest, add_sample_record_success)
{
    if (sample_record_head == NULL) {
        sample_record_head = (SampleRecordNode *)malloc(sizeof(SampleRecordNode));
    }
    EXPECT_EQ(add_sample_record(1, 1ULL * VNPU_STATS_NS_PER_MS, ns_now()), ENPU_SUCCESS);
    remove_sample_records(sample_record_head);
}

TEST_F(VnpuStatsTest, core_limit_off_still_samples)
{
    MOCKER(is_core_limit).stubs().will(invoke(stub_is_core_limiter));
    MOCKER(read).stubs().will(invoke(stub_read_success));
    EXPECT_TRUE(is_random_sampling());
}

TEST_F(VnpuStatsTest, open_fail_return_false)
{
    MOCKER(get_random_fd).stubs().will(invoke(stub_get_random_fd));
    EXPECT_FALSE(is_random_sampling());
}

TEST_F(VnpuStatsTest, read_fail_return_false)
{
    MOCKER(read).stubs().will(invoke(stub_read_fail));
    EXPECT_FALSE(is_random_sampling());
}

TEST_F(VnpuStatsTest, random_sampling_test)
{
    MOCKER(read).stubs().will(invoke(stub_read_success));
    EXPECT_TRUE(is_random_sampling());
}

// vnpu_stats_init 失败时会 destroy g_sampling_mutex，此时必须拒绝采样，
// 否则 sampling_begin 会对已销毁的 mutex 加锁（EINVAL 且不加锁 = 静默失去互斥）。
TEST_F(VnpuStatsTest, random_sampling_false_when_not_inited)
{
    MOCKER(read).stubs().will(invoke(stub_read_success));
    ASSERT_TRUE(g_vnpu_stats_inited);
    g_vnpu_stats_inited = false;
    EXPECT_FALSE(is_random_sampling());
    g_vnpu_stats_inited = true;
}

TEST_F(VnpuStatsTest, is_stream_capture_test)
{
    rtStream_t stm = nullptr;
    rtStreamCaptureStatus *status = nullptr;
    EXPECT_EQ(is_stream_capture(stm, status), ENPU_SUCCESS);
}

TEST_F(VnpuStatsTest, get_kernel_count_test)
{
    rtModel_t mdl = nullptr;
    int count = get_kernel_count(mdl);
    EXPECT_EQ(count, -1);
}

// ---- vnpu_stats_init / do_init ----

// vnpu_stats_init validates inputs, tolerates shm mapping failure, and re-initializes cleanly.
TEST_F(VnpuStatsTest, vnpu_stats_init_input_and_map_failure_paths)
{
    // Already inited: early success without touching any state.
    ASSERT_TRUE(g_vnpu_stats_inited);
    EXPECT_EQ(vnpu_stats_init(get_vnpu_shm_id()), ENPU_SUCCESS);

    g_vnpu_stats_inited = false;
    EXPECT_EQ(vnpu_stats_init(NULL), ENPU_FAIL);
    EXPECT_EQ(vnpu_stats_init(""), ENPU_FAIL);

    // Mapping failure: the freshly allocated record head is released.
    FreeSampleRecords();
    MOCKER(map_share_mem).stubs().will(returnValue((void *)NULL));
    EXPECT_EQ(vnpu_stats_init(get_vnpu_shm_id()), ENPU_FAIL);
    GlobalMockObject::reset();
    EXPECT_EQ(sample_record_head, nullptr);

    // Recovery: a clean re-init succeeds and restores the record head.
    EXPECT_EQ(vnpu_stats_init(get_vnpu_shm_id()), ENPU_SUCCESS);
    EXPECT_NE(sample_record_head, nullptr);
    EXPECT_TRUE(g_vnpu_stats_inited);
}

// do_init keeps data on the INITIALIZED fast path, and reclaims garbage or
// stuck INITIALIZING magic by re-zeroing the whole shm.
TEST_F(VnpuStatsTest, vnpu_stats_do_init_magic_paths)
{
    uint8_t id = get_vnpu_id();

    // Garbage magic: full (re-)initialization zeroes previous stats.
    atomic_store(&shm_->buckets[id][0].bucket_start_ns, ns_now());
    atomic_store(&shm_->buckets[id][0].sum_block_dim, 77);
    atomic_store(&shm_->magic_number, 0x12345678U);
    FreeSampleRecords();
    g_vnpu_stats_inited = false;
    EXPECT_EQ(vnpu_stats_init(get_vnpu_shm_id()), ENPU_SUCCESS);
    EXPECT_EQ(atomic_load(&shm_->buckets[id][0].sum_block_dim), 0U);
    EXPECT_EQ(atomic_load(&shm_->magic_number), (uint_fast32_t)VNPU_STATS_MAGIC_INITIALIZED);

    // INITIALIZED magic: fast path keeps existing data untouched.
    atomic_store(&shm_->buckets[id][1].bucket_start_ns, ns_now());
    atomic_store(&shm_->buckets[id][1].sum_block_dim, 55);
    FreeSampleRecords();
    g_vnpu_stats_inited = false;
    EXPECT_EQ(vnpu_stats_init(get_vnpu_shm_id()), ENPU_SUCCESS);
    EXPECT_EQ(atomic_load(&shm_->buckets[id][1].sum_block_dim), 55U);

    // Stuck INITIALIZING magic (no concurrent finisher): timeout (~3ms) reclaims and re-zeroes.
    atomic_store(&shm_->buckets[id][2].bucket_start_ns, ns_now());
    atomic_store(&shm_->buckets[id][2].sum_block_dim, 99);
    atomic_store(&shm_->magic_number, VNPU_STATS_MAGIC_INITIALIZING);
    FreeSampleRecords();
    g_vnpu_stats_inited = false;
    EXPECT_EQ(vnpu_stats_init(get_vnpu_shm_id()), ENPU_SUCCESS);
    EXPECT_EQ(atomic_load(&shm_->buckets[id][2].sum_block_dim), 0U);
    EXPECT_EQ(atomic_load(&shm_->magic_number), (uint_fast32_t)VNPU_STATS_MAGIC_INITIALIZED);
}

// ---- guard branches ----

// Not-inited guards turn every stats entry point into a safe no-op / failure.
TEST_F(VnpuStatsTest, vnpu_stats_guards_not_inited_and_invalid_setter_id)
{
    uint8_t id = get_vnpu_id();
    vnpu_stats_aggregate_t agg;

    g_vnpu_stats_inited = false;
    vnpu_stats_record(id, 32, 1); // no-op
    EXPECT_EQ(vnpu_stats_query(id, &agg), ENPU_FAIL);
    vnpu_stats_set_local_avg_duration(id, 2ULL * VNPU_STATS_NS_PER_MS, 3); // no-op
    EXPECT_EQ(vnpu_stats_get_avg_duration_ns(id), 0);
    g_vnpu_stats_inited = true;

    // Out-of-range vnpu_id on the setter path is a no-op as well.
    vnpu_stats_set_local_avg_duration(MAX_VNPU, 2ULL * VNPU_STATS_NS_PER_MS, 3);
    EXPECT_EQ(vnpu_stats_get_avg_duration_ns(MAX_VNPU), 0);
}

// get ignores slots whose timestamp lies in the future (clock skew guard).
TEST_F(VnpuStatsTest, vnpu_stats_get_avg_duration_future_ts_ignored)
{
    uint8_t id = get_vnpu_id();
    atomic_store(&shm_->avg_slots[id][0].sum_ns, 5ULL * VNPU_STATS_NS_PER_MS);
    atomic_store(&shm_->avg_slots[id][0].count, 1);
    atomic_store(&shm_->avg_slots[id][0].ts_ns, ns_now() + 10ULL * VNPU_STATS_WINDOW_NS);
    EXPECT_EQ(vnpu_stats_get_avg_duration_ns(id), 0);
}

// query excludes buckets whose start lies in the future.
TEST_F(VnpuStatsTest, vnpu_stats_query_excludes_future_bucket)
{
    uint8_t id = get_vnpu_id();
    atomic_store(&shm_->buckets[id][3].bucket_start_ns, ns_now() + 2ULL * VNPU_STATS_WINDOW_NS);
    atomic_store(&shm_->buckets[id][3].sum_block_dim, 64);
    atomic_store(&shm_->buckets[id][3].launch_count, 2);

    vnpu_stats_aggregate_t agg;
    EXPECT_EQ(vnpu_stats_query(id, &agg), ENPU_SUCCESS);
    EXPECT_EQ(agg.sum_block_dim, 0U);
    EXPECT_EQ(agg.launch_count, 0U);
}

// A second set call reuses the already-allocated process slot with the latest value.
TEST_F(VnpuStatsTest, vnpu_stats_set_local_avg_duration_reuses_slot)
{
    uint8_t id = get_vnpu_id();
    vnpu_stats_set_local_avg_duration(id, 3ULL * VNPU_STATS_NS_PER_MS, 2);
    vnpu_stats_set_local_avg_duration(id, 6ULL * VNPU_STATS_NS_PER_MS, 2);
    EXPECT_EQ(vnpu_stats_get_avg_duration_ns(id), (int64_t)(6ULL * VNPU_STATS_NS_PER_MS));
}

// ---- sampling state machine ----

// is_stream_capture maps runtime errors to ENPU_FAIL and success to ENPU_SUCCESS.
TEST_F(VnpuStatsTest, is_stream_capture_success_and_fail)
{
    rtStreamCaptureStatus status = RT_STREAM_CAPTURE_STATUS_NONE;
    rtStream_t stm = reinterpret_cast<rtStream_t>(0x1);

    HookEntryGuard failEntry(HOOK_rtStreamGetCaptureInfo, reinterpret_cast<void *>(StubCaptureFail));
    EXPECT_EQ(is_stream_capture(stm, &status), ENPU_FAIL);

    HookEntryGuard okEntry(HOOK_rtStreamGetCaptureInfo, reinterpret_cast<void *>(StubCaptureNone));
    EXPECT_EQ(is_stream_capture(stm, &status), ENPU_SUCCESS);
}

// sampling_begin aborts on capture query failure, captured streams, a sample
// already in flight, and start-event creation/record failures. Every aborted
// begin leaves g_sampling false and the mutex locked; the paired sampling_end
// releases it through its not-sampling branch.
TEST_F(VnpuStatsTest, sampling_begin_failure_paths)
{
    rtStream_t stm = reinterpret_cast<rtStream_t>(0x2);

    {
        HookEntryGuard entry(HOOK_rtStreamGetCaptureInfo, reinterpret_cast<void *>(StubCaptureFail));
        sampling_begin(stm);
    }
    EXPECT_FALSE(atomic_load(&g_sampling));
    sampling_end(stm);

    {
        HookEntryGuard entry(HOOK_rtStreamGetCaptureInfo, reinterpret_cast<void *>(StubCaptureActive));
        sampling_begin(stm);
    }
    EXPECT_FALSE(atomic_load(&g_sampling));
    sampling_end(stm);

    g_sample_flag = true; // pretend a sample is already in flight
    {
        HookEntryGuard entry(HOOK_rtStreamGetCaptureInfo, reinterpret_cast<void *>(StubCaptureNone));
        sampling_begin(stm);
    }
    EXPECT_FALSE(atomic_load(&g_sampling));
    sampling_end(stm);
    g_sample_flag = false;

    {
        HookEntryGuard captureEntry(HOOK_rtStreamGetCaptureInfo, reinterpret_cast<void *>(StubCaptureNone));
        HookEntryGuard createEntry(HOOK_rtEventCreateExWithFlag, reinterpret_cast<void *>(StubEventCreateFail));
        sampling_begin(stm);
    }
    EXPECT_FALSE(atomic_load(&g_sampling));
    EXPECT_FALSE(g_sample_flag);
    EXPECT_EQ(g_sample_start_event, nullptr);
    EXPECT_EQ(g_sample_stream, nullptr);
    sampling_end(stm);

    // 被调方违约：返回失败却写了出参。全局量仍必须被清空。
    {
        HookEntryGuard captureEntry(HOOK_rtStreamGetCaptureInfo, reinterpret_cast<void *>(StubCaptureNone));
        HookEntryGuard createEntry(HOOK_rtEventCreateExWithFlag,
                                   reinterpret_cast<void *>(StubEventCreateFailButWrites));
        sampling_begin(stm);
    }
    EXPECT_FALSE(atomic_load(&g_sampling));
    EXPECT_FALSE(g_sample_flag);
    EXPECT_EQ(g_sample_start_event, nullptr);
    EXPECT_EQ(g_sample_stream, nullptr);
    sampling_end(stm);

    // record 失败：事件已 destroy，句柄必须置 NULL，否则全局量里留着悬垂句柄。
    {
        HookEntryGuard captureEntry(HOOK_rtStreamGetCaptureInfo, reinterpret_cast<void *>(StubCaptureNone));
        HookEntryGuard createEntry(HOOK_rtEventCreateExWithFlag, reinterpret_cast<void *>(StubEventCreateOk));
        HookEntryGuard recordEntry(HOOK_rtEventRecord, reinterpret_cast<void *>(StubEventRecordFail));
        sampling_begin(stm);
    }
    EXPECT_FALSE(atomic_load(&g_sampling));
    EXPECT_FALSE(g_sample_flag);
    EXPECT_EQ(g_sample_start_event, nullptr);
    EXPECT_EQ(g_sample_stream, nullptr);
    sampling_end(stm);
}

// sampling_begin followed by sampling_end spawns the detached sample_sync
// thread, which records one sample (kernel_count=1, elapsed=2ms) into the list.
TEST_F(VnpuStatsTest, sampling_end_records_and_sample_sync_collects)
{
    rtStream_t stm = reinterpret_cast<rtStream_t>(0x3);

    HookEntryGuard captureEntry(HOOK_rtStreamGetCaptureInfo, reinterpret_cast<void *>(StubCaptureNone));
    HookEntryGuard createEntry(HOOK_rtEventCreateExWithFlag, reinterpret_cast<void *>(StubEventCreateOk));
    HookEntryGuard recordEntry(HOOK_rtEventRecord, reinterpret_cast<void *>(StubEventRecordOk));
    HookEntryGuard destroyEntry(HOOK_rtEventDestroy, reinterpret_cast<void *>(StubEventDestroyOk));
    HookEntryGuard syncEntry(HOOK_rtStreamSynchronize, reinterpret_cast<void *>(StubStreamSyncOk));
    HookEntryGuard elapsedEntry(HOOK_rtEventElapsedTime, reinterpret_cast<void *>(StubEventElapsed2ms));

    sampling_begin(stm);
    EXPECT_TRUE(atomic_load(&g_sampling));
    EXPECT_TRUE(g_sample_flag);

    sampling_end(stm); // spawns sample_sync
    EXPECT_FALSE(atomic_load(&g_sampling));

    ASSERT_TRUE(WaitUntilFlagCleared(&g_sample_flag, 2ULL * NS_PER_S)) << "sample_sync thread did not finish";
    ASSERT_NE(sample_record_head, nullptr);
    SampleRecordNode *rec = sample_record_head->next;
    ASSERT_NE(rec, nullptr);
    EXPECT_EQ(rec->kernel_count, 1);
    EXPECT_EQ(rec->sample_time, 2ULL * VNPU_STATS_NS_PER_MS);
    EXPECT_GT(rec->timestamp, 0ULL);
}

// sampling_graph_end counts model kernels via rtModelGetStreams/rtStreamGetTasks
// (2 streams with 3+4 tasks) and feeds the count into the sample record.
TEST_F(VnpuStatsTest, sampling_graph_end_counts_model_kernels)
{
    rtStream_t stm = reinterpret_cast<rtStream_t>(0x4);
    rtModel_t mdl = reinterpret_cast<rtModel_t>(0x5);

    HookEntryGuard captureEntry(HOOK_rtStreamGetCaptureInfo, reinterpret_cast<void *>(StubCaptureNone));
    HookEntryGuard createEntry(HOOK_rtEventCreateExWithFlag, reinterpret_cast<void *>(StubEventCreateOk));
    HookEntryGuard recordEntry(HOOK_rtEventRecord, reinterpret_cast<void *>(StubEventRecordOk));
    HookEntryGuard destroyEntry(HOOK_rtEventDestroy, reinterpret_cast<void *>(StubEventDestroyOk));
    HookEntryGuard syncEntry(HOOK_rtStreamSynchronize, reinterpret_cast<void *>(StubStreamSyncOk));
    HookEntryGuard elapsedEntry(HOOK_rtEventElapsedTime, reinterpret_cast<void *>(StubEventElapsed1ms));
    HookEntryGuard streamsEntry(HOOK_rtModelGetStreams, reinterpret_cast<void *>(StubModelStreamsTwo));
    HookEntryGuard tasksEntry(HOOK_rtStreamGetTasks, reinterpret_cast<void *>(StubStreamTasksThreeOrFour));

    sampling_begin(stm);
    sampling_graph_end(stm, mdl);

    ASSERT_TRUE(WaitUntilFlagCleared(&g_sample_flag, 2ULL * NS_PER_S)) << "sample_sync thread did not finish";
    ASSERT_NE(sample_record_head, nullptr);
    SampleRecordNode *rec = sample_record_head->next;
    ASSERT_NE(rec, nullptr);
    EXPECT_EQ(rec->kernel_count, 7); // 3 tasks + 4 tasks
}

// get_kernel_count propagates every introspection failure as -1.
TEST_F(VnpuStatsTest, get_kernel_count_error_paths)
{
    rtModel_t mdl = reinterpret_cast<rtModel_t>(0x6);

    {
        HookEntryGuard entry(HOOK_rtModelGetStreams, reinterpret_cast<void *>(StubModelStreamsFailAlways));
        EXPECT_EQ(get_kernel_count(mdl), -1); // sizing call fails
    }
    {
        HookEntryGuard entry(HOOK_rtModelGetStreams, reinterpret_cast<void *>(StubModelStreamsZeroNum));
        EXPECT_EQ(get_kernel_count(mdl), -1); // model has no streams
    }
    {
        HookEntryGuard entry(HOOK_rtModelGetStreams, reinterpret_cast<void *>(StubModelStreamsFailSecond));
        EXPECT_EQ(get_kernel_count(mdl), -1); // filling call fails
    }
    {
        HookEntryGuard streamsEntry(HOOK_rtModelGetStreams, reinterpret_cast<void *>(StubModelStreamsOne));
        HookEntryGuard tasksEntry(HOOK_rtStreamGetTasks, reinterpret_cast<void *>(StubStreamTasksFail));
        EXPECT_EQ(get_kernel_count(mdl), -1); // task query fails
    }
    {
        HookEntryGuard streamsEntry(HOOK_rtModelGetStreams, reinterpret_cast<void *>(StubModelStreamsOne));
        HookEntryGuard tasksEntry(HOOK_rtStreamGetTasks, reinterpret_cast<void *>(StubStreamTasksZero));
        EXPECT_EQ(get_kernel_count(mdl), -1); // stream has zero tasks
    }
}
