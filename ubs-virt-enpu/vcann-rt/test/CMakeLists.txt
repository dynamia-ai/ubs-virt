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

add_compile_definitions(_GLIBCXX_USE_CXX11_ABI=1)

include(${PROJECT_SOURCE_DIR}/scripts/cmake/gtest.cmake)
include(${PROJECT_SOURCE_DIR}/scripts/cmake/mockcpp.cmake)

set(VNPU_SRC_FILES
    ${PROJECT_SOURCE_DIR}/../common/src/allocation_id.c
    ${PROJECT_SOURCE_DIR}/src/core_limiter.c
    ${PROJECT_SOURCE_DIR}/src/hbm_sampler.c
    ${PROJECT_SOURCE_DIR}/src/stream_drain.c
    ${PROJECT_SOURCE_DIR}/src/mem_limiter.c
    ${PROJECT_SOURCE_DIR}/src/npu_manager.c
    ${PROJECT_SOURCE_DIR}/src/log.c
    ${PROJECT_SOURCE_DIR}/src/vnpu_stats.c
    ${PROJECT_SOURCE_DIR}/src/memory_tracker.c
    ${PROJECT_SOURCE_DIR}/src/shm_manager.c
    ${PROJECT_SOURCE_DIR}/src/swap_buffer_shm.c
    ${PROJECT_SOURCE_DIR}/src/swap_executor.c
    ${PROJECT_SOURCE_DIR}/src/swap_hook.c
    ${PROJECT_SOURCE_DIR}/src/swap_monitor_thread.c
    ${PROJECT_SOURCE_DIR}/src/ascend/device.c
    ${PROJECT_SOURCE_DIR}/src/ascend/event.c
    ${PROJECT_SOURCE_DIR}/src/ascend/graph.c
    ${PROJECT_SOURCE_DIR}/src/ascend/hook.c
    ${PROJECT_SOURCE_DIR}/src/ascend/kernel.c
    ${PROJECT_SOURCE_DIR}/src/ascend/memory.c
    ${PROJECT_SOURCE_DIR}/src/ascend/task.c
    ${PROJECT_SOURCE_DIR}/src/utils/config.c
    ${PROJECT_SOURCE_DIR}/src/utils/dcmi_wrapper.c
    ${PROJECT_SOURCE_DIR}/src/utils/hash_map.c
    ${PROJECT_SOURCE_DIR}/src/utils/utils.c
)

set(VNPU_TESTCASE_FILES
    ${CMAKE_CURRENT_SOURCE_DIR}/testcase/ascend/device_test.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/testcase/ascend/event_test.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/testcase/ascend/graph_test.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/testcase/ascend/memory_test.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/testcase/ascend/kernel_test.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/testcase/ascend/task_test.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/testcase/utils/utils_test.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/testcase/utils/config_test.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/testcase/utils/hash_map_test.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/testcase/npu_manager_test.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/testcase/core_limiter_test.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/testcase/log_test.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/testcase/mem_limiter_test.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/testcase/mem_limiter_elastic_test.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/testcase/shm_manager_test.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/testcase/vnpu_stats_test.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/testcase/swap_module_test.cpp
)

set(VNPU_STUB_FILES
    ${CMAKE_CURRENT_SOURCE_DIR}/stub/runtime_stub.c
    ${CMAKE_CURRENT_SOURCE_DIR}/stub/dcmi_stub.c
)

add_executable(vnpu_test
    ${VNPU_SRC_FILES}
    ${VNPU_TESTCASE_FILES}
    ${VNPU_STUB_FILES}
)

target_include_directories(vnpu_test PRIVATE
    ${PROJECT_SOURCE_DIR}/src/include
    ${PROJECT_SOURCE_DIR}/../common/include
    ${CMAKE_CURRENT_SOURCE_DIR}/stub
)

target_link_libraries(vnpu_test PRIVATE
    gtest
    gtest_main
    mockcpp
    pthread
    rt
    dl
    c_sec
)

foreach(stub_file IN LISTS VNPU_STUB_FILES)
    set_source_files_properties(${stub_file} PROPERTIES
        COMPILE_FLAGS "-Wno-unused-parameter"
    )
endforeach()

target_compile_definitions(vnpu_test
    PRIVATE
    _GNU_SOURCE
)

target_compile_options(vnpu_test PRIVATE
    -Wall -Wextra -Wformat-nonliteral -Wformat-security -Wformat-y2k -Wfloat-equal
    $<$<COMPILE_LANGUAGE:CXX>:-std=c++14>
    -fPIC
    -fstack-protector-strong
    -fvisibility=hidden
    -fno-common
    -O1
)

target_link_options(vnpu_test PRIVATE
    -Wl,-z,relro,-z,now,-z,noexecstack
    -s
)

if(ENABLE_COVERAGE)
    add_compile_options(-fprofile-arcs -ftest-coverage)
    target_compile_options(vnpu_test PRIVATE -fprofile-arcs -ftest-coverage)
    target_link_options(vnpu_test PRIVATE -fprofile-arcs -ftest-coverage)
endif()
# mem-swap 模块单测(自制轻量断言框架, 各含独立 main):
# 与 gtest 主套件隔离, 避免全局状态(共享内存/线程)互相干扰。
# swap_monitor_test/swap_monitor_thread_test 暂未启用: 其 mock 与真实实现符号冲突,
# 需重构 mock 链接架构后启用(源仓中同样处于禁用状态)。
foreach(swap_test memory_tracker_test swap_executor_test swap_hook_test)
    add_executable(${swap_test}
        ${CMAKE_CURRENT_SOURCE_DIR}/testcase/${swap_test}.c
        ${VNPU_SRC_FILES}
        ${VNPU_STUB_FILES}
        # swap 用例依赖 shm/vmm mock 桩(源仓配套, 辅助符号与真实实现无冲突)
        ${CMAKE_CURRENT_SOURCE_DIR}/stub/mock_shm_stub.c
        ${CMAKE_CURRENT_SOURCE_DIR}/stub/rts_mem_stub.c
    )
    target_include_directories(${swap_test} PRIVATE
        ${PROJECT_SOURCE_DIR}/src/include
        ${PROJECT_SOURCE_DIR}/../common/include
        ${CMAKE_CURRENT_SOURCE_DIR}/stub
    )
    target_link_libraries(${swap_test} PRIVATE
        pthread
        rt
        dl
        c_sec
    )
    target_compile_definitions(${swap_test} PRIVATE _GNU_SOURCE)
    target_compile_options(${swap_test} PRIVATE -Wall -Wextra -Wno-unused-parameter -fPIC -O1)
    if(ENABLE_COVERAGE)
        # 编译插桩由全局 add_compile_options 覆盖, 此处补链接 gcov 运行时, 否则缺 __gcov_init 等符号
        target_link_options(${swap_test} PRIVATE -fprofile-arcs -ftest-coverage)
    endif()
    add_test(NAME ${swap_test} COMMAND ${swap_test})
endforeach()
