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

#ifndef QEMU_ISOL_TUNER_H
#define QEMU_ISOL_TUNER_H

#include <vector>

#include "rapidjson/document.h"

#include "base_tuner.h"

class QemuIsolTuner : public BaseTuner {
public:
    std::string name() const override;
    std::string category() const override;
    std::string principle() const override;
    std::string advice() const override;

    bool check() override;
    void apply() override;

private:
    enum class ResultCode : int
    {
        SUCCESS = 0,
        FALSE,
        ERROR,
    };
    static void parseHostData(const std::string &rawJson);
    void findLastInfer();
    QemuIsolTuner::ResultCode checkApply();
    int findPIndex(const std::vector<std::string> &tokens);
    QemuIsolTuner::ResultCode isAllF(const std::string &line);
    std::vector<std::string> splitBySpace(const std::string &line);
    static int interval;
};

#endif // QEMU_ISOL_TUNER_H
