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

#include "data_file_snapshot.h"

#include <stdexcept>
#include <system_error>

namespace fs = std::filesystem;

DataFileSnapshot::DataFileSnapshot(const fs::path &sourcePath)
{
    const fs::path canonicalPath = fs::canonical(sourcePath);
    backupPath_ = canonicalPath;
    backupPath_ += ".bak";

    try {
        fs::copy_file(canonicalPath, backupPath_, fs::copy_options::overwrite_existing);
        stream_.open(backupPath_);
        if (!stream_.is_open()) {
            throw std::runtime_error("Unable to open data snapshot: " + backupPath_.string());
        }
    } catch (...) {
        cleanup();
        throw;
    }
}

DataFileSnapshot::~DataFileSnapshot()
{
    cleanup();
}

std::ifstream &DataFileSnapshot::stream() noexcept
{
    return stream_;
}

void DataFileSnapshot::cleanup() noexcept
{
    if (stream_.is_open()) {
        stream_.close();
    }

    if (!backupPath_.empty()) {
        std::error_code error;
        fs::remove(backupPath_, error);
    }
}
