# Copyright (c) Huawei Technologies Co., Ltd. 2026. All rights reserved.
# Licensed under Mulan PSL v2.
foreach(name allocation_registry config_manager swap_order)
    add_executable(${name}_test ${name}_test.c)
    target_link_libraries(${name}_test PRIVATE enpu_manager ${C_SEC_SHARED_LIBRARY})
    target_include_directories(${name}_test PRIVATE ${C_SEC_INCLUDE})
    target_compile_options(${name}_test PRIVATE -Wall -Wextra -UNDEBUG)
    add_test(NAME ${name}_test COMMAND ${name}_test)
endforeach()
