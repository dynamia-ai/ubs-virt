/*
 * Copyright (c) Huawei Technologies Co., Ltd. 2025-2025. All rights reserved.
 */
#include <chrono>
#include <filesystem>
#include <fstream>
#include <string>
#include <thread>

#include <gtest/gtest.h>
#include <mockcpp/GlobalMockObject.h>
#include <mockcpp/mockcpp.hpp>

#include "common/data_dump_thread.h"

namespace fs = std::filesystem;
// 假设 DUMP_FILE_SIZE_THRESHOLD 定义为 1024 字节
constexpr int DUMP_FILE_SIZE_THRESHOLD = 1024;

// 测试用例类
class DataDumpThreadTest : public ::testing::Test {
protected:
    void SetUp() override
    {
        // 创建测试文件
        std::ofstream testFile("test_data.json");
        testFile << "test content";
        testFile.close();

        // 创建临时目录
        fs::create_directory("test_dump_dir");
    }

    void TearDown() override
    {
        // 清理测试文件和目录
        fs::remove("test_data.json");
        fs::remove_all("test_dump_dir");
    }

    std::shared_ptr<MutexContext> createContext()
    {
        return std::make_shared<MutexContext>();
    }
};

// 测试构造函数
TEST_F(DataDumpThreadTest, ConstructorTest)
{
    DataDumpThread thread("test_data.json", createContext(), 1);
    EXPECT_TRUE(thread.dataDir.empty());
    EXPECT_FALSE(thread.dataBaseName.empty());
    mockcpp::GlobalMockObject::verify();
    mockcpp::GlobalMockObject::reset();
}

// 测试 isOversize() 方法
TEST_F(DataDumpThreadTest, IsOversizeTest)
{
    DataDumpThread thread("test_data.json", createContext(), 1);

    // 文件大小小于阈值，返回 false
    EXPECT_FALSE(thread.isOversize());

    // 创建一个大文件
    std::ofstream bigFile("test_big.json", std::ios::binary);
    bigFile.seekp(DUMP_FILE_SIZE_THRESHOLD + 1);
    bigFile.write("", 0);
    bigFile.close();

    DataDumpThread threadBig("test_big.json", createContext(), 1);
    EXPECT_FALSE(threadBig.isOversize());

    fs::remove("test_big.json");
    mockcpp::GlobalMockObject::verify();
    mockcpp::GlobalMockObject::reset();
}

// 测试 doDumpData() 方法
TEST_F(DataDumpThreadTest, DoDumpDataTest)
{
    const fs::path sourcePath = "test_dump_dir/test_data.json";
    std::ofstream source(sourcePath);
    source << "test content";
    source.close();
    DataDumpThread thread(sourcePath.string(), createContext(), 1);

    // 执行备份
    thread.doDumpData();

    // 验证原文件被清空
    std::ifstream src(sourcePath);
    EXPECT_TRUE(src);
    std::string content((std::istreambuf_iterator<char>(src)), std::istreambuf_iterator<char>());
    EXPECT_TRUE(content.empty());

    fs::path dumpPath;
    for (const auto &entry : fs::directory_iterator("test_dump_dir")) {
        const std::string fileName = entry.path().filename().string();
        if (fileName.rfind("test_data_", 0) == 0) {
            dumpPath = entry.path();
            break;
        }
    }
    ASSERT_FALSE(dumpPath.empty());
    std::ifstream backup(dumpPath);
    ASSERT_TRUE(backup);
    std::string backupContent((std::istreambuf_iterator<char>(backup)), std::istreambuf_iterator<char>());
    EXPECT_EQ(backupContent, "test content");
    mockcpp::GlobalMockObject::verify();
    mockcpp::GlobalMockObject::reset();
}

// 测试线程启动和停止
TEST_F(DataDumpThreadTest, ThreadLifecycleTest)
{
    DataDumpThread thread("test_data.json", createContext(), 1);

    // 启动线程
    thread.start();

    // 等待线程运行
    std::this_thread::sleep_for(std::chrono::seconds(1));

    // 停止线程
    thread.stop();

    // 验证线程已停止
    EXPECT_FALSE(thread.monitorThread.joinable());
    mockcpp::GlobalMockObject::verify();
    mockcpp::GlobalMockObject::reset();
}
