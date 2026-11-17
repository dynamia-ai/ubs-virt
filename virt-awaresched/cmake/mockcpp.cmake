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

# build_mockcpp.cmake
if(TARGET mockcpp::mockcpp)
    return()
endif()

set(DEPS_DIR "${CMAKE_BINARY_DIR}/_deps" CACHE PATH "Dependencies directory")
set(MOCKCPP_INSTALL_DIR "${DEPS_DIR}/mockcpp")
if(EXISTS "${MOCKCPP_INSTALL_DIR}")
    message(STATUS "mockcpp already built. Install directory found: ${MOCKCPP_INSTALL_DIR}")

    add_library(mockcpp INTERFACE)
    target_include_directories(mockcpp SYSTEM INTERFACE ${MOCKCPP_INSTALL_DIR}/include)
    target_link_libraries(mockcpp INTERFACE ${MOCKCPP_INSTALL_DIR}/lib/libmockcpp.a)

    return()
else()
    message(STATUS "mockcpp already built. Install directory found: ${MOCKCPP_INSTALL_DIR}")
endif()

include(FetchContent)

# --- Paths & Metadata ---
set(MOCKCPP_URL "https://gitcode.com/cann-src-third-party/mockcpp/releases/download/v2.7-h2/mockcpp-2.7.tar.gz")
set(MOCKCPP_SHA256 "73ab0a8b6d1052361c2cebd85e022c0396f928d2e077bf132790ae3be766f603")

string(SHA256 CONFIG_HASH
        "${CMAKE_CXX_COMPILER_ID}-${CMAKE_CXX_COMPILER_VERSION}-${CMAKE_GENERATOR}"
)

# --- Download ---
message(STATUS "Downloading mockcpp src from ${MOCKCPP_URL}")

# 根据cmake版本决定是否使用DOWNLOAD_EXTRACT_TIMESTAMP参数
if(CMAKE_VERSION VERSION_GREATER_EQUAL 3.24)
    FetchContent_Declare(
            _mockcpp_src
            URL      ${MOCKCPP_URL}
            URL_HASH SHA256=${MOCKCPP_SHA256}
            DOWNLOAD_EXTRACT_TIMESTAMP TRUE
    )
else()
    FetchContent_Declare(
            _mockcpp_src
            URL      ${MOCKCPP_URL}
            URL_HASH SHA256=${MOCKCPP_SHA256}
    )
endif()

FetchContent_Populate(_mockcpp_src)

# --- Patch: apply ARM64 support patch ---

# 使用仓库内补丁文件，避免构建时依赖外部网络资源
if (EXISTS "${_mockcpp_src_SOURCE_DIR}/src/JmpCodeAARCH64.h")
    message(STATUS "ARM64 patch already applied, skipping.")
else()
    set(PATCH_FILE "${CMAKE_CURRENT_LIST_DIR}/patches/mockcpp_support_arm64.patch")

    if(EXISTS "${PATCH_FILE}")
        message(STATUS "Applying mockcpp patch: ${PATCH_FILE}")
        # Normalize line endings (keep your existing logic)
        file(READ "${PATCH_FILE}" _PATCH_CONTENT)
        string(REGEX MATCHALL "diff --git a/([A-Za-z0-9/._-]+)" _MATCHES "${_PATCH_CONTENT}")
        foreach(_MATCH IN LISTS _MATCHES)
            string(REGEX REPLACE "diff --git a/(.+)" "\\1" _FILE "${_MATCH}")
            if(EXISTS "${_mockcpp_src_SOURCE_DIR}/${_FILE}")
                execute_process(
                        COMMAND sed -i "s/\\r\$//" "${_mockcpp_src_SOURCE_DIR}/${_FILE}"
                        WORKING_DIRECTORY "${_mockcpp_src_SOURCE_DIR}"
                )
            endif()
        endforeach()

        # Apply patch with better error handling
        execute_process(
                COMMAND patch -p1 --verbose
                INPUT_FILE "${PATCH_FILE}"
                WORKING_DIRECTORY "${_mockcpp_src_SOURCE_DIR}"
                RESULT_VARIABLE PATCH_RESULT
                OUTPUT_VARIABLE PATCH_OUTPUT
                ERROR_VARIABLE PATCH_ERROR
        )

        if(NOT PATCH_RESULT EQUAL 0)
            message(FATAL_ERROR
                    "Failed to apply mockcpp patch!\n"
                    "Patch file: ${PATCH_FILE}\n"
                    "Exit code: ${PATCH_RESULT}\n"
                    "Output:\n${PATCH_OUTPUT}\n${PATCH_ERROR}"
            )
        else()
            message(STATUS "Patch applied successfully.")
        endif()
    else()
        message(WARNING "Patch file not found: ${PATCH_FILE}")
    endif()
endif()

# Step 3: 独立构建（关键！）
file(MAKE_DIRECTORY ${DEPS_DIR})
set(SUB_BUILD "${DEPS_DIR}/mockcpp-build")
set(SUB_INSTALL "${DEPS_DIR}/mockcpp-install")
file(REMOVE_RECURSE "${SUB_BUILD}")
file(MAKE_DIRECTORY "${SUB_BUILD}")

# 调用 cmake 时显式指定 -S (source) 和 -B (build)
execute_process(
        COMMAND
        ${CMAKE_COMMAND}
        -S "${_mockcpp_src_SOURCE_DIR}"     # 子模块源码根
        -B "${SUB_BUILD}"                   # 子模块构建根
        -DCMAKE_BUILD_TYPE=${CMAKE_BUILD_TYPE}
        -DCMAKE_INSTALL_PREFIX=${SUB_INSTALL}
        -DCMAKE_C_FLAGS=-Wall\ -Wextra\ -Wformat-nonliteral\ -Wformat-security\ -Wformat-y2k\ -Wfloat-equal\ -std=gnu11\ -fPIC\ -fstack-protector-strong\ -fvisibility=hidden\ -fno-common
        -DCMAKE_CXX_FLAGS=-Wall\ -Wextra\ -Wformat-nonliteral\ -Wformat-security\ -Wformat-y2k\ -Wfloat-equal\ -std=c++11\ -fPIC\ -fstack-protector-strong\ -fvisibility=hidden\ -fno-common
        -DCMAKE_EXE_LINKER_FLAGS=-Wl,-z,relro,-z,now,-z,noexecstack\ -s
        RESULT_VARIABLE res
)

if(res)
    message(FATAL_ERROR "Configure failed")
endif()

# 构建
execute_process(
        COMMAND ${CMAKE_COMMAND} --build "${SUB_BUILD}" --target install -j8
)

# 确保源目录存在
if(EXISTS "${SUB_INSTALL}")
    file(REMOVE_RECURSE "${MOCKCPP_INSTALL_DIR}")
    file(MAKE_DIRECTORY "${MOCKCPP_INSTALL_DIR}")
    file(COPY "${SUB_INSTALL}/" DESTINATION "${MOCKCPP_INSTALL_DIR}")
    add_library(mockcpp INTERFACE)
    target_include_directories(mockcpp SYSTEM INTERFACE ${MOCKCPP_INSTALL_DIR}/include)
    target_link_libraries(mockcpp INTERFACE ${MOCKCPP_INSTALL_DIR}/lib/libmockcpp.a)
endif()