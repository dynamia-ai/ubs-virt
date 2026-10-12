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

#ifndef HALT_POLL_TUNER_H
#define HALT_POLL_TUNER_H

#include <vector>

#include "rapidjson/document.h"

#include "base_tuner.h"
#include "data_struct.h"

using IPIData = std::pair<IpiInterrupt, time_t>;

class HaltPollTuner : public BaseTuner {
public:
    std::string name() const override;
    std::string category() const override;
    std::string principle() const override;
    std::string advice() const override;

    bool check() override;
    void apply() override;

private:
    static IPIData parseIPIData(const std::string &rawJson);
    bool checkApply();
    void findLastInfer();
    static uint64_t decideHaltpoll(uint64_t maxIPIPerSec);

    std::vector<IPIData> inferData;
    uint64_t haltpoll;
};

#endif // HALT_POLL_TUNER_H
