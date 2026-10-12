/*
 * Copyright (c) Huawei Technologies Co., Ltd. 2026-2026. All rights reserved.
 */

#include <filesystem>
#include <fstream>
#include <stdexcept>
#include <string>
#include <system_error>

#include <unistd.h>

#include <gtest/gtest.h>

#include "optimizer/tuner/data_file_snapshot.h"

namespace fs = std::filesystem;

class DataFileSnapshotTest : public ::testing::Test {
protected:
    void SetUp() override
    {
        testDirectory_ = fs::temp_directory_path() / ("ubs-opt-data-snapshot-" + std::to_string(getpid()));
        fs::remove_all(testDirectory_);
        fs::create_directories(testDirectory_);
        sourcePath_ = testDirectory_ / "data.json";
        backupPath_ = testDirectory_ / "data.json.bak";
    }

    void TearDown() override
    {
        std::error_code error;
        fs::remove_all(testDirectory_, error);
    }

    static void writeFile(const fs::path &path, const std::string &content)
    {
        std::ofstream output(path, std::ios::trunc);
        ASSERT_TRUE(output.is_open());
        output << content;
    }

    fs::path testDirectory_;
    fs::path sourcePath_;
    fs::path backupPath_;
};

TEST_F(DataFileSnapshotTest, CopiesSourceAndRemovesBackupAtScopeExit)
{
    writeFile(sourcePath_, "first line\nsecond line\n");

    {
        DataFileSnapshot snapshot(sourcePath_);
        ASSERT_TRUE(fs::exists(backupPath_));

        std::string line;
        ASSERT_TRUE(std::getline(snapshot.stream(), line));
        EXPECT_EQ(line, "first line");
    }

    EXPECT_FALSE(fs::exists(backupPath_));
    EXPECT_TRUE(fs::exists(sourcePath_));
}

TEST_F(DataFileSnapshotTest, ReplacesStaleBackup)
{
    writeFile(sourcePath_, "current data\n");
    writeFile(backupPath_, "stale data\n");

    DataFileSnapshot snapshot(sourcePath_);
    std::string line;
    ASSERT_TRUE(std::getline(snapshot.stream(), line));
    EXPECT_EQ(line, "current data");
}

TEST_F(DataFileSnapshotTest, RemovesBackupDuringExceptionUnwinding)
{
    writeFile(sourcePath_, "current data\n");

    EXPECT_THROW(
        {
            DataFileSnapshot snapshot(sourcePath_);
            EXPECT_TRUE(fs::exists(backupPath_));
            throw std::runtime_error("test error");
        },
        std::runtime_error);

    EXPECT_FALSE(fs::exists(backupPath_));
}

TEST_F(DataFileSnapshotTest, RejectsMissingSourceWithoutLeavingBackup)
{
    EXPECT_THROW({ DataFileSnapshot snapshot(sourcePath_); }, fs::filesystem_error);
    EXPECT_FALSE(fs::exists(backupPath_));
}
