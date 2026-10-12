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

#ifndef SOCKET_IO_H
#define SOCKET_IO_H

#include <cerrno>
#include <chrono>
#include <cstddef>

#include <poll.h>
#include <sys/socket.h>
#include <unistd.h>

namespace socket_io {

inline bool writeExact(int fd, const void *data, size_t size)
{
    const auto *bytes = static_cast<const unsigned char *>(data);
    size_t written = 0;
    while (written < size) {
        const ssize_t result = send(fd, bytes + written, size - written, MSG_NOSIGNAL);
        if (result > 0) {
            written += static_cast<size_t>(result);
            continue;
        }
        if (result < 0 && errno == EINTR) {
            continue;
        }
        return false;
    }
    return true;
}

inline bool readExact(int fd, void *data, size_t size, int timeoutMs)
{
    auto *bytes = static_cast<unsigned char *>(data);
    size_t received = 0;
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(timeoutMs);

    while (received < size) {
        const auto remaining =
            std::chrono::duration_cast<std::chrono::milliseconds>(deadline - std::chrono::steady_clock::now());
        if (remaining.count() <= 0) {
            return false;
        }

        pollfd descriptor{fd, POLLIN, 0};
        const int pollResult = poll(&descriptor, 1, static_cast<int>(remaining.count()));
        if (pollResult < 0 && errno == EINTR) {
            continue;
        }
        if (pollResult <= 0 || (descriptor.revents & POLLIN) == 0) {
            return false;
        }

        const ssize_t result = read(fd, bytes + received, size - received);
        if (result > 0) {
            received += static_cast<size_t>(result);
            continue;
        }
        if (result < 0 && errno == EINTR) {
            continue;
        }
        return false;
    }
    return true;
}

} // namespace socket_io

#endif // SOCKET_IO_H
