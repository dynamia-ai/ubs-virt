cmake_minimum_required(VERSION 3.15)
project(mcs-runtime)
set(CMAKE_SKIP_RPATH TRUE)

file(GLOB MCS_SRC
    src/mcs_config.c
    src/mcs_device.c
    src/mcs_log.c
    src/mcs_mem.c
    src/mcs_rtapi.c
    src/mcs_sched.c
    src/mcs_intercept.c
    common/hook.c
    common/mem_limiter.c
    common/memory.c
)

include(${CMAKE_CURRENT_SOURCE_DIR}/scripts/cmake/securec.cmake)

set(COMPILE_OPTIONS
    -O2
    -Wall
    -Wextra
    -std=gnu11
    -fPIC
    -fstack-protector-strong
    -fvisibility=hidden
    -fno-common
    -D_GNU_SOURCE
)

message(STATUS "Building MCS library")

add_library(mcs SHARED ${MCS_SRC})
add_custom_command(
    TARGET mcs POST_BUILD
    COMMAND chmod 400 $<TARGET_FILE:mcs>
)

target_include_directories(mcs PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/src
    ${CMAKE_CURRENT_SOURCE_DIR}/common
)

target_link_libraries(mcs PRIVATE c_sec)

target_link_options(mcs PRIVATE
    -Wl,-z,relro,-z,now,-z,noexecstack
    -Wl,--no-as-needed
    -ldl
    -lrt
    -lpthread
)

target_compile_options(mcs PRIVATE ${COMPILE_OPTIONS})
