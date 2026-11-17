/*
 * Copyright (c) Huawei Technologies Co., Ltd. 2026-2026. All rights reserved.
 * ubs-virt-enpu is licensed under Mulan PSL v2.
 * You can use this software according to the terms and conditions of the Mulan PSL v2.
 * You may obtain a copy of Mulan PSL v2 at:
 *          http://license.coscl.org.cn/MulanPSL2
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND,
 * EITHER EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT,
 * MERCHANTABILITY OR FIT FOR A PARTICULAR PURPOSE.
 * See the Mulan PSL v2 for more details.
 */

#include <gtest/gtest.h>
#include "mcs_rtapi.h"
#include "runtime_hook.h"
#include "runtime_stub.h"

TEST(RtApiTest, ResolvesAllRealFunctionsAndHookTableEntries)
{
    reset_runtime_stub();
    load_real_functions();
    EXPECT_NE(real_rtSetDevice, nullptr);
    EXPECT_NE(real_rtGetDeviceInfo, nullptr);
    EXPECT_NE(real_rtGetAiCoreCount, nullptr);
    EXPECT_NE(real_rtKernelLaunch, nullptr);
    EXPECT_NE(real_rtKernelLaunchWithHandle, nullptr);
    EXPECT_NE(real_rtKernelLaunchWithHandleV2, nullptr);
    EXPECT_NE(real_rtKernelLaunchWithFlag, nullptr);
    EXPECT_NE(real_rtKernelLaunchWithFlagV2, nullptr);
    EXPECT_NE(real_rtVectorCoreKernelLaunchWithHandle, nullptr);
    EXPECT_NE(real_rtVectorCoreKernelLaunch, nullptr);
    EXPECT_NE(real_rtKernelLaunchEx, nullptr);
    EXPECT_NE(real_rtKernelLaunchFwk, nullptr);
    EXPECT_NE(real_rtLaunchKernelByFuncHandle, nullptr);
    EXPECT_NE(real_rtLaunchKernelByFuncHandleV2, nullptr);
    EXPECT_NE(real_rtLaunchKernelByFuncHandleV3, nullptr);
    EXPECT_NE(real_rtsLaunchKernelWithDevArgs, nullptr);
    EXPECT_NE(real_rtsLaunchKernelWithConfig, nullptr);
    EXPECT_NE(real_rtsLaunchKernelWithHostArgs, nullptr);
    EXPECT_NE(real_rtLaunchKernelWithArgsArray, nullptr);
    EXPECT_NE(real_rtsGetResInCurrentThread, nullptr);

    for (int i = 0; i < RUNTIME_ENTRY_END; ++i) {
        EXPECT_NE(rt_library_entry[i].func_ptr, nullptr) << rt_library_entry[i].name;
    }
}

TEST(RtApiTest, RepeatedLoadsKeepResolvedPointers)
{
    load_real_functions();
    load_rt_libraries();
    SUCCEED();
}
