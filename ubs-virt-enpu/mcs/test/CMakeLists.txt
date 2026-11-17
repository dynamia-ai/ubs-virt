# -----------------------------------------------------------------------------------------------------------
# Copyright (c) Huawei Technologies Co., Ltd. 2026-2026. All rights reserved.
# ubs-virt-enpu is licensed under Mulan PSL v2.
# You can use this software according to the terms and conditions of the Mulan PSL v2.
# You may obtain a copy of Mulan PSL v2 at:
#          http://license.coscl.org.cn/MulanPSL2
# THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND,
# EITHER EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT,
# MERCHANTABILITY OR FIT FOR A PARTICULAR PURPOSE.
# See the Mulan PSL v2 for more details.
# -----------------------------------------------------------------------------------------------------------

include(${PROJECT_SOURCE_DIR}/scripts/cmake/gtest.cmake)

set(MCS_TEST_SRC_FILES
    ${PROJECT_SOURCE_DIR}/src/mcs_config.c
    ${PROJECT_SOURCE_DIR}/src/mcs_device.c
    ${PROJECT_SOURCE_DIR}/src/mcs_log.c
    ${PROJECT_SOURCE_DIR}/src/mcs_mem.c
    ${PROJECT_SOURCE_DIR}/src/mcs_rtapi.c
    ${PROJECT_SOURCE_DIR}/src/mcs_sched.c
    ${PROJECT_SOURCE_DIR}/src/mcs_intercept.c
    ${PROJECT_SOURCE_DIR}/common/hook.c
    ${PROJECT_SOURCE_DIR}/common/mem_limiter.c
)

set(MCS_TESTCASE_FILES
    testcase/config_test.cpp
    testcase/device_test.cpp
    testcase/mem_limiter_test.cpp
    testcase/sched_test.cpp
    testcase/log_test.cpp
    testcase/rtapi_test.cpp
    testcase/intercept_test.cpp
)

function(configure_mcs_test target)
    target_include_directories(${target} PRIVATE
        ${PROJECT_SOURCE_DIR}/src
        ${PROJECT_SOURCE_DIR}/common
        ${CMAKE_CURRENT_SOURCE_DIR}/stub
    )
    target_link_libraries(${target} PRIVATE
        gtest
        gtest_main
        pthread
        rt
        dl
        c_sec
    )
    target_compile_definitions(${target} PRIVATE _GNU_SOURCE)
    target_compile_options(${target} PRIVATE
        -Wall -Wextra -Wformat-nonliteral -Wformat-security -Wformat-y2k -Wfloat-equal
        $<$<COMPILE_LANGUAGE:C>:-std=gnu11>
        $<$<COMPILE_LANGUAGE:CXX>:-std=c++14>
        -fPIC
        -fstack-protector-strong
        -fno-common
        -O1
    )
    target_link_options(${target} PRIVATE
        -Wl,--wrap=dlsym
        -Wl,--wrap=dlopen
        -Wl,-z,relro,-z,now,-z,noexecstack
    )
    if(ENABLE_COVERAGE)
        target_compile_options(${target} PRIVATE -fprofile-arcs -ftest-coverage)
        target_link_options(${target} PRIVATE -fprofile-arcs -ftest-coverage)
    endif()
    add_test(NAME ${target} COMMAND ${target})
endfunction()

add_executable(mcs_test
    ${MCS_TEST_SRC_FILES}
    ${MCS_TESTCASE_FILES}
    stub/runtime_stub.c
)
configure_mcs_test(mcs_test)
target_compile_definitions(mcs_test PRIVATE MCS_TEST_MAIN_SUITE)

add_executable(mcs_memory_test
    ${PROJECT_SOURCE_DIR}/src/mcs_config.c
    ${PROJECT_SOURCE_DIR}/src/mcs_mem.c
    ${PROJECT_SOURCE_DIR}/common/mem_limiter.c
    ${PROJECT_SOURCE_DIR}/common/memory.c
    testcase/memory_test.cpp
    stub/runtime_stub.c
)
configure_mcs_test(mcs_memory_test)
