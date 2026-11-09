cmake_minimum_required(VERSION 3.15)
project(enpu-manager LANGUAGES C)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_SKIP_RPATH TRUE)

message(STATUS "Building enpu-manager")

file(GLOB_RECURSE SRC_FILES CONFIGURE_DEPENDS src/*.c)
list(APPEND SRC_FILES ${CMAKE_CURRENT_SOURCE_DIR}/../../common/src/allocation_id.c)
file(GLOB_RECURSE HEADER_FILES include/*.h)

set(COMPILE_OPTIONS
    -Wall
    -O2
    -fPIC
    -fstack-protector-strong
)

include(${CMAKE_CURRENT_SOURCE_DIR}/scripts/cmake/securec.cmake)

find_path(UUID_INCLUDE_DIR NAMES uuid/uuid.h)
find_library(UUID_LIBRARY NAMES uuid)
if(NOT UUID_INCLUDE_DIR OR NOT UUID_LIBRARY)
    message(FATAL_ERROR "libuuid development files not found; install libuuid-devel (RPM) or uuid-dev (Debian)")
endif()
message(STATUS "Found libuuid: ${UUID_LIBRARY}")

set(LINK_LIBS pthread rt cjson ${UUID_LIBRARY} ascendcl c_sec)

if(DEFINED ENV{ASCEND_HOME_PATH})
    message(STATUS "CANN environment detected: $ENV{ASCEND_HOME_PATH}")
    list(APPEND INCLUDE_DIRS
        $ENV{ASCEND_HOME_PATH}/include
        $ENV{ASCEND_HOME_PATH}/include/experiment
        $ENV{ASCEND_HOME_PATH}/pkg_inc/
        $ENV{ASCEND_HOME_PATH}/pkg_inc/runtime
    )
    list(APPEND LINK_DIRS
        $ENV{ASCEND_HOME_PATH}/lib64
        $ENV{ASCEND_HOME_PATH}/lib64/drv
        $ENV{ENPU_ASCEND_DRIVER_PATH}/driver/lib64
        $ENV{ENPU_ASCEND_DRIVER_PATH}/driver/lib64/driver
    )
    find_library(DCMI_LIB dcmi
        PATHS ${LINK_DIRS}
        NO_DEFAULT_PATH)
    if(DCMI_LIB)
        message(STATUS "Found libdcmi: ${DCMI_LIB}")
        list(APPEND LINK_LIBS ${DCMI_LIB})
    else()
        message(WARNING "libdcmi.so not found in known paths, falling back to -ldcmi")
        list(APPEND LINK_LIBS dcmi)
    endif()
    set(CANN_ENABLED ON)
else()
    message(STATUS "CANN environment not detected, building without DCMI")
    set(CANN_ENABLED OFF)
endif()

add_library(enpu_manager SHARED ${SRC_FILES})
set_target_properties(enpu_manager PROPERTIES
    OUTPUT_NAME enpu_manager
    VERSION 1.0.0
    SOVERSION 1
)

target_include_directories(enpu_manager
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/internal>
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/../../common/include>
        $<INSTALL_INTERFACE:include>
)
target_include_directories(enpu_manager SYSTEM PRIVATE ${UUID_INCLUDE_DIR})

if(CANN_ENABLED)
    target_include_directories(enpu_manager SYSTEM PRIVATE ${INCLUDE_DIRS})
    target_link_directories(enpu_manager PRIVATE ${LINK_DIRS})
    set(ENPU_CANN_INCLUDE_DIRS "${INCLUDE_DIRS}" CACHE INTERNAL "CANN include dirs for tests")
    set(ENPU_CANN_LINK_DIRS "${LINK_DIRS}" CACHE INTERNAL "CANN link dirs for tests")
    set(ENPU_CANN_LINK_LIBS "${LINK_LIBS}" CACHE INTERNAL "CANN link libs for tests")
endif()

target_link_libraries(enpu_manager PRIVATE ${LINK_LIBS})
target_compile_options(enpu_manager PRIVATE ${COMPILE_OPTIONS})

option(BUILD_TESTS "Build unit tests" OFF)
option(ENABLE_COVERAGE "Enable code coverage" OFF)

if(BUILD_TESTS)
    enable_testing()
    if(ENABLE_COVERAGE)
        message(STATUS "Coverage enabled")
        target_compile_options(enpu_manager PRIVATE --coverage -fprofile-arcs -ftest-coverage)
        target_link_options(enpu_manager PRIVATE --coverage)
    endif()
    if(EXISTS ${CMAKE_CURRENT_SOURCE_DIR}/test/CMakeLists.txt)
        add_subdirectory(test)
    else()
        file(GLOB TEST_FILES test/*.c test/*.cpp)
        if(TEST_FILES)
            add_executable(enpu_manager_test ${TEST_FILES} ${SRC_FILES})
            target_include_directories(enpu_manager_test PRIVATE 
                ${CMAKE_CURRENT_SOURCE_DIR}/include
                ${CMAKE_CURRENT_SOURCE_DIR}/internal)
            target_link_libraries(enpu_manager_test PRIVATE ${LINK_LIBS})
            if(ENABLE_COVERAGE)
                target_compile_options(enpu_manager_test PRIVATE --coverage -fprofile-arcs -ftest-coverage)
                target_link_options(enpu_manager_test PRIVATE --coverage)
            endif()
            add_test(NAME enpu_manager_test COMMAND enpu_manager_test)
        endif()
    endif()
endif()

message(STATUS "CMakeLists.txt configuration complete")
