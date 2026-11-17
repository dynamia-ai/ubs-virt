 # -----------------------------------------------------------------------------------------------------------
 # Copyright (c) Huawei Technologies Co., Ltd. 2026-2026. All rights reserved.
 # ubs-virt is licensed under Mulan PSL v2.
 # You can use this software according to the terms and conditions of the Mulan PSL v2.
 # You may obtain a copy of Mulan PSL v2 at:
 #          http://license.coscl.org.cn/MulanPSL2
 # THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND,
 # EITHER EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT,
 # MERCHANTABILITY OR FIT FOR A PARTICULAR PURPOSE.
 # See the Mulan PSL v2 for more details.
 # -----------------------------------------------------------------------------------------------------------

 # Third-party dependency policy:
 # 1. Prefer the community package (gtest-devel) provided by openEuler.
 # 2. Fall back to downloading the upstream release only when the community
 #    package is unavailable.

 find_package(GTest 1.14 QUIET)

 if(GTest_FOUND)
     message(STATUS "Using community GTest: ${GTEST_INCLUDE_DIRS}")
     if(NOT TARGET gtest)
         add_library(gtest ALIAS GTest::gtest)
     endif()
     if(NOT TARGET GTest::gtest_main)
         add_library(gtest_main ALIAS GTest::gtest_main)
     endif()
     return()
 endif()

 include(FetchContent)

 set(BUILD_GMOCK ON CACHE BOOL "" FORCE)
 set(BUILD_GTEST ON CACHE BOOL "" FORCE)
 set(INSTALL_GTEST OFF CACHE BOOL "" FORCE)
 set(gtest_force_shared_crt ON CACHE BOOL "" FORCE)
 set(GOOGLETEST_URL "https://gitcode.com/cann-src-third-party/googletest/releases/download/v1.14.0/googletest-1.14.0.tar.gz")

 message(STATUS "Community GTest not found. Downloading googletest src from ${GOOGLETEST_URL}")

 # 根据cmake版本决定是否使用DOWNLOAD_EXTRACT_TIMESTAMP参数
 if(CMAKE_VERSION VERSION_GREATER_EQUAL 3.24)
     FetchContent_Declare(
             googletest
             URL ${GOOGLETEST_URL}
             DOWNLOAD_EXTRACT_TIMESTAMP TRUE
     )
 else()
     FetchContent_Declare(
             googletest
             URL ${GOOGLETEST_URL}
     )
 endif()


 FetchContent_MakeAvailable(googletest)
