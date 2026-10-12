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

#ifndef DATA_FILE_SNAPSHOT_H
#define DATA_FILE_SNAPSHOT_H

#include <filesystem>
#include <fstream>

class DataFileSnapshot final {
public:
    explicit DataFileSnapshot(const std::filesystem::path &sourcePath);
    ~DataFileSnapshot();

    DataFileSnapshot(const DataFileSnapshot &) = delete;
    DataFileSnapshot &operator=(const DataFileSnapshot &) = delete;
    DataFileSnapshot(DataFileSnapshot &&) = delete;
    DataFileSnapshot &operator=(DataFileSnapshot &&) = delete;

    std::ifstream &stream() noexcept;

private:
    void cleanup() noexcept;

    std::filesystem::path backupPath_;
    std::ifstream stream_;
};

#endif // DATA_FILE_SNAPSHOT_H
