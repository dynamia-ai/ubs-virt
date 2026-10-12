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

#include "ipi_collector.h"
#include "ipi_trace.h"
#include "log/ebpf_logger_macros.h"
#include "receivers/ebpf_receiver.h"

static int handle_event(void *ctx, void *data, unsigned long size)
{
    if (data == nullptr || size < sizeof(IpiInterrupt)) {
        EBPF_LOG_WARN("Event data broken, skipping...");
        return 1;
    }
    auto event = static_cast<IpiInterrupt *>(data);
    EBPFReceiver::getInstance().accumulate(*event);
    return 0;
}

IPICollector &IPICollector::getInstance()
{
    static IPICollector instance;
    return instance;
}

CollectorStatus IPICollector::launchCollecting()
{
    ipiTrace = ipi_trace__open_and_load();
    if (!ipiTrace) {
        return CollectorStatus::OPEN_BPF_PROGRAM_FAILED;
    }
    struct ring_buffer *rb = nullptr;
    if (ipiTrace->maps.events) {
        rb = ring_buffer__new(bpf_map__fd(ipiTrace->maps.events), handle_event, nullptr, nullptr);
        if (!rb) {
            ipi_trace__destroy(ipiTrace);
            return CollectorStatus::CREAT_RINGBUF_FAILED;
        }
    }

    auto err = ipi_trace__attach(ipiTrace);
    if (err) {
        ipi_trace__destroy(ipiTrace);
        ring_buffer__free(rb);
        return CollectorStatus::LOAD_BPF_FAILED;
    }
    ringbuf.reset(rb);
    stopFlag = false;
    collecting_ = true;
    collectionThread_ = std::make_unique<std::thread>(&Collector::collectionTask, this);
    return CollectorStatus::SUCCESS;
}

void IPICollector::stopCollecting()
{
    Collector::stopCollecting();
    ipi_trace__destroy(ipiTrace);
}
