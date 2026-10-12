/*
 * Copyright (c) Huawei Technologies Co., Ltd. 2025-2025. All rights reserved.
 */
#include <cerrno>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <thread>
#include <vector>

#include <sys/socket.h>
#include <unistd.h>

#include <gtest/gtest.h>
#include <mockcpp/GlobalMockObject.h>
#include <mockcpp/mockcpp.hpp>
#include "rapidjson/document.h"

#include "client/collector/receivers/vsock_client.h"
#include "socket_io.h"
#include "utils.h"

void Clean1()
{
    mockcpp::GlobalMockObject::verify();
    mockcpp::GlobalMockObject::reset();
}

TEST(VsockClientTest, CreateFail)
{
    DataTable data;
    MOCKER(createAndConnectVsock).stubs().will(returnValue(-1));
    bool res = sendData(data);
    EXPECT_EQ(res, false);
    Clean1();
}

TEST(VsockClientTest, SendSuccess)
{
    DataTable data1;

    MOCKER(getPortBind).expects(once()).will(returnValue(10101));
    MOCKER(createAndConnectVsock).stubs().with(10101).will(returnValue(1));
    MOCKER(send).stubs().will(returnValue(static_cast<ssize_t>(sizeof(DataTable))));
    MOCKER(close).stubs().with(any()).will(returnValue(0));
    bool res = sendData(data1);
    EXPECT_EQ(res, true);
    Clean1();
}

TEST(VsockClientTest, SendFailsWhenCompletePayloadCannotBeWritten)
{
    DataTable data;
    errno = EIO;
    MOCKER(getPortBind).expects(once()).will(returnValue(10101));
    MOCKER(createAndConnectVsock).stubs().with(10101).will(returnValue(1));
    MOCKER(send)
        .expects(exactly(2))
        .will(returnValue(static_cast<ssize_t>(1)))
        .then(returnValue(static_cast<ssize_t>(-1)));
    MOCKER(close).stubs().with(any()).will(returnValue(0));
    EXPECT_FALSE(sendData(data));
    Clean1();
}

TEST(VsockClientTest, WriteExactHandlesFragmentedWrites)
{
    int sockets[2];
    ASSERT_EQ(socketpair(AF_UNIX, SOCK_STREAM, 0, sockets), 0);
    const std::string payload = "fragmented payload";
    std::thread writer([&]() {
        const size_t split = payload.size() / 2;
        EXPECT_EQ(write(sockets[0], payload.data(), split), static_cast<ssize_t>(split));
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
        EXPECT_EQ(write(sockets[0], payload.data() + split, payload.size() - split),
                  static_cast<ssize_t>(payload.size() - split));
        close(sockets[0]);
    });

    std::string received(payload.size(), '\0');
    EXPECT_TRUE(socket_io::readExact(sockets[1], received.data(), received.size(), 1000));
    EXPECT_EQ(received, payload);
    writer.join();
    close(sockets[1]);
}

TEST(VsockClientTest, ReadExactRejectsEarlyClose)
{
    int sockets[2];
    ASSERT_EQ(socketpair(AF_UNIX, SOCK_STREAM, 0, sockets), 0);
    const std::string partial = "short";
    ASSERT_EQ(write(sockets[0], partial.data(), partial.size()), static_cast<ssize_t>(partial.size()));
    close(sockets[0]);

    char received[16]{};
    EXPECT_FALSE(socket_io::readExact(sockets[1], received, sizeof(received), 1000));
    close(sockets[1]);
}

TEST(VsockClientTest, SockFail)
{
    MOCKER(socket).stubs().with(any()).will(returnValue(-1));
    int res = createAndConnectVsock(1);
    EXPECT_EQ(res, -1);
    Clean1();
}

TEST(VsockClientTest, ConnectFail)
{
    MOCKER(socket).stubs().with(any()).will(returnValue(1));
    MOCKER(connect).stubs().with(any()).will(returnValue(-1));
    MOCKER(close).stubs().with(any()).will(returnValue(0));
    int res = createAndConnectVsock(1);
    EXPECT_EQ(res, -1);
    Clean1();
}

TEST(VsockClientTest, ConnectSuccess)
{
    MOCKER(socket).stubs().with(any()).will(returnValue(1));
    MOCKER(connect).stubs().with(any()).will(returnValue(1));
    int res = createAndConnectVsock(1);
    EXPECT_EQ(res, 1);
    Clean1();
}

TEST(VsockClientTest, getPortBindCase1)
{
    std::string filename = "/usr/local/sbin/ubs-optimizer/config.json";
    std::ofstream outfile(filename);
    outfile << "{\"sampling_interval\": 30,\n"
               "\"bind_port\":,\n"
               "\"vm_name\": \"openeuler\",\n"
               "\"npu_type\": \"d802\",\n"
               "\"system\" : {}}"
            << std::endl;
    outfile.close();
    int res = getPortBind();
    EXPECT_EQ(res, -1);
    Clean1();
}

TEST(VsockClientTest, getPortBindCase2)
{
    std::string filename = "/usr/local/sbin/ubs-optimizer/config.json";
    std::ofstream outfile(filename);
    outfile << "{\"sampling_interval\": 30,\n"
               "\"bind_port\": \"abc\",\n"
               "\"vm_name\": \"openeuler\",\n"
               "\"npu_type\": \"d802\",\n"
               "\"system\" : {}}"
            << std::endl;
    outfile.close();
    int res = getPortBind();
    EXPECT_EQ(res, -1);
    Clean1();
}

TEST(VsockClientTest, getPortBindCase3)
{
    std::string filename = "/usr/local/sbin/ubs-optimizer/config.json";
    std::ofstream outfile(filename);
    outfile << "{\"sampling_interval\": 30,\n"
               "\"bind_port\": 512,\n"
               "\"vm_name\": \"openeuler\",\n"
               "\"npu_type\": \"d802\",\n"
               "\"system\" : {}}"
            << std::endl;
    outfile.close();
    int res = getPortBind();
    EXPECT_EQ(res, -1);
    Clean1();
}

TEST(VsockClientTest, getPortBindCase4)
{
    std::string filename = "/usr/local/sbin/ubs-optimizer/config.json";
    std::ofstream outfile(filename);
    outfile << "{\"sampling_interval\": 30,\n"
               "\"bind_port\": 10101,\n"
               "\"vm_name\": \"openeuler\",\n"
               "\"npu_type\": \"d802\",\n"
               "\"system\" : {}}"
            << std::endl;
    outfile.close();
    int res = getPortBind();
    EXPECT_EQ(res, 10101);
    Clean1();
}
