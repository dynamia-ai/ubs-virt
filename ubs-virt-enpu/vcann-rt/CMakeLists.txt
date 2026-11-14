cmake_minimum_required(VERSION 3.15)
project(vcann-runtime)
set(CMAKE_SKIP_RPATH TRUE)
message(STATUS "CMakeList start ${CMAKE_CURRENT_SOURCE_DIR}")

file(GLOB SRC_LIST
    src/*.c
    src/ascend/*.c
    src/tools/*.c
    src/utils/*.c
)
list(FILTER SRC_LIST EXCLUDE REGEX ".*/src/tools/monitor\\.c$")
list(APPEND SRC_LIST ${CMAKE_CURRENT_SOURCE_DIR}/../common/src/allocation_id.c)

set(SECURE_COMPILE_OPTIONS
    -O2
    -Wall
    -Wextra
    -Wformat-nonliteral
    -Wformat-security
    -Wformat-y2k
    -Wfloat-equal
    -std=gnu11
    -fPIC
    -fstack-protector-strong
    -fvisibility=hidden
    -fno-common
)

include(${CMAKE_CURRENT_SOURCE_DIR}/scripts/cmake/securec.cmake)

set(INCLUDE_DIRS
    $ENV{ASCEND_HOME_PATH}/include
    $ENV{ASCEND_HOME_PATH}/pkg_inc/runtime
    $ENV{ASCEND_HOME_PATH}/pkg_inc/runtime/runtime
    $ENV{ASCEND_HOME_PATH}/pkg_inc/runtime/runtime/rts
    $ENV{ASCEND_HOME_PATH}/pkg_inc/profiling
    $ENV{ASCEND_HOME_PATH}/pkg_inc/
    $ENV{ASCEND_HOME_PATH}/pkg_inc/msprof
    $ENV{ENPU_ASCEND_DRIVER_PATH}/driver/include
    $ENV{ASCEND_HOME_PATH}/include/experiment/runtime
    $ENV{ASCEND_HOME_PATH}/include/experiment/msprof
)

set(LINK_DIRS
    $ENV{ASCEND_HOME_PATH}/runtime
    $ENV{ASCEND_HOME_PATH}/lib64
    $ENV{ENPU_ASCEND_DRIVER_PATH}/driver/lib64/driver
)

set(LINK_LIBS dcmi)

message(STATUS "CMakeList build start")
add_library(vruntime SHARED src/ascend/hook.c ${SRC_LIST})
add_custom_command(
    TARGET vruntime POST_BUILD
    COMMAND chmod 400 $<TARGET_FILE:vruntime>
)

target_link_options(vruntime PRIVATE
    -Wl,-z,relro,-z,now,-z,noexecstack
    -Wl,-z,defs
    -s
)

target_include_directories(vruntime PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/src/include
    ${CMAKE_CURRENT_SOURCE_DIR}/../common/include
)
target_include_directories(vruntime SYSTEM PRIVATE ${INCLUDE_DIRS})
target_link_directories(vruntime PRIVATE ${LINK_DIRS})
target_link_libraries(vruntime PRIVATE ${LINK_LIBS} c_sec pthread dl rt)
target_compile_options(vruntime PRIVATE ${SECURE_COMPILE_OPTIONS})
target_compile_definitions(vruntime PRIVATE _GLIBCXX_USE_CXX11_ABI=0)

add_executable(enpu-monitor src/tools/monitor.c ${SRC_LIST}
    ${CMAKE_CURRENT_SOURCE_DIR}/../common/src/vnpu_metrics_reader.c)
add_custom_command(
    TARGET enpu-monitor POST_BUILD
    COMMAND chmod 500 $<TARGET_FILE:enpu-monitor>
)

target_link_options(enpu-monitor PRIVATE
    -Wl,-z,relro,-z,now,-z,noexecstack
    -s
)

target_include_directories(enpu-monitor PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/src/include
    ${CMAKE_CURRENT_SOURCE_DIR}/../common/include
)
target_include_directories(enpu-monitor SYSTEM PRIVATE ${INCLUDE_DIRS})
target_link_directories(enpu-monitor PRIVATE ${LINK_DIRS})
target_link_libraries(enpu-monitor PRIVATE ${LINK_LIBS} c_sec pthread dl rt)
target_compile_options(enpu-monitor PRIVATE ${SECURE_COMPILE_OPTIONS})
target_compile_definitions(enpu-monitor PRIVATE _GLIBCXX_USE_CXX11_ABI=0)

option(BUILD_TESTS "Build tests" OFF)
option(ENABLE_COVERAGE "Enable coverage reporting" OFF)

if(BUILD_TESTS)
    enable_testing()
    add_subdirectory(test)
endif()

message(STATUS "CMakeList end")
