/*
 * Copyright (c) Huawei Technologies Co., Ltd. 2025-2025. All rights reserved.
 *
 * ubs-optimizer is licensed under Mulan PSL v2.
 * You can use this software according to the terms and conditions of the Mulan PSL v2.
 * You may obtain a copy of Mulan PSL v2 at:
 *          http://license.coscl.org.cn/MulanPSL2
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND,
 * EITHER EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT,
 * MERCHANTABILITY OR FIT FOR A PARTICULAR PURPOSE.
 * See the Mulan PSL v2 for more details.
 */

#include "sched_collector.h"
#include "log/ebpf_logger_macros.h"
#include "receivers/ebpf_receiver.h"
#include "sched_trace.h"

static int handle_event(void *ctx, void *data, unsigned long size)
{
    if (data == nullptr || size < sizeof(SchedSwitch)) {
        EBPF_LOG_WARN("Event data broken, skipping...");
        return 1;
    }
    auto event = static_cast<SchedSwitch *>(data);
    EBPFReceiver::getInstance().accumulate(*event);
    return 0;
}

SchedCollector &SchedCollector::getInstance()
{
    static SchedCollector schedCollector;
    return schedCollector;
}

CollectorStatus SchedCollector::launchCollecting()
{
    schedTrace = sched_trace__open_and_load();
    if (!schedTrace) {
        return CollectorStatus::OPEN_BPF_PROGRAM_FAILED;
    }
    struct ring_buffer *rb = nullptr;
    if (schedTrace->maps.events) {
        rb = ring_buffer__new(bpf_map__fd(schedTrace->maps.events), handle_event, nullptr, nullptr);
        if (!rb) {
            sched_trace__destroy(schedTrace);
            return CollectorStatus::CREAT_RINGBUF_FAILED;
        }
    }

    auto err = sched_trace__attach(schedTrace);
    if (err) {
        sched_trace__destroy(schedTrace);
        ring_buffer__free(rb);
        return CollectorStatus::LOAD_BPF_FAILED;
    }
    ringbuf.reset(rb);
    stopFlag = false;
    collecting_ = true;
    collectionThread_ = std::make_unique<std::thread>(&Collector::collectionTask, this);
    return CollectorStatus::SUCCESS;
}

void SchedCollector::stopCollecting()
{
    Collector::stopCollecting();
    sched_trace__destroy(schedTrace);
}
