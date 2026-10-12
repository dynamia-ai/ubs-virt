/*
 * Copyright (c) Huawei Technologies Co., Ltd. 2026-2026. All rights reserved.
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

#ifndef PROCESS_PIPE_H
#define PROCESS_PIPE_H

#include <cstdio>

class ProcessPipe final {
public:
    explicit ProcessPipe(FILE *pipe) noexcept : pipe_(pipe) {}

    ~ProcessPipe()
    {
        close();
    }

    ProcessPipe(const ProcessPipe &) = delete;
    ProcessPipe &operator=(const ProcessPipe &) = delete;
    ProcessPipe(ProcessPipe &&) = delete;
    ProcessPipe &operator=(ProcessPipe &&) = delete;

    explicit operator bool() const noexcept
    {
        return pipe_ != nullptr;
    }

    FILE *get() const noexcept
    {
        return pipe_;
    }

    int close() noexcept
    {
        if (pipe_ == nullptr) {
            return 0;
        }
        FILE *pipe = pipe_;
        pipe_ = nullptr;
        return ::pclose(pipe);
    }

private:
    FILE *pipe_;
};

#endif // PROCESS_PIPE_H
