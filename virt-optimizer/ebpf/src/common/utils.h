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

#ifndef UBS_OPT_UTILS_H
#define UBS_OPT_UTILS_H

#include <optional>
#include <string>
#include <vector>

#include <fcntl.h>

enum class NPUType
{
    UNKNOWN = 0,
    ALLOWED_NPU_D802,
    ALLOWED_NPU_D803
};

namespace utils {
pid_t forkWithDir(const char *path);
std::optional<pid_t> getDaemonPID(const char *path);
bool checkProcessName(int pid, const char *name);
bool checkRunning(const char *path, const char *name);
std::optional<std::string> getConfigString(const std::string &key);
std::optional<unsigned int> getConfigUint(const std::string &key);
bool isValidVmName(const std::string &vmName);
std::string getVmName();
void writeToDisk(const std::vector<std::string> &jsonBuffer, const std::string &outputPath);
NPUType getNPUType();
std::string NPUType2Str(NPUType npuType);
} // namespace utils

#endif
