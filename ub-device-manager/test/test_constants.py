##########################################################################################################
# Copyright (c) Huawei Technologies Co., Ltd. 2026. All rights reserved.
#
# ub-device-manager is licensed under the Mulan PSL v2.
# You can use this software according to the terms and conditions of the Mulan PSL v2.
# You may obtain a copy of Mulan PSL v2 at:
#      http://license.coscl.org.cn/MulanPSL2
# THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR
# IMPLIED, INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY OR FIT FOR A PARTICULAR PURPOSE.
# See the Mulan PSL v2 for more details.
##########################################################################################################
"""Unit tests for :mod:`ub_device_manager.constants`."""
from ub_device_manager import constants


async def test_context_keys_are_unique_strings():
    keys = [
        value
        for name, value in vars(constants).items()
        if name.endswith("_CONTEXT_KEY")
    ]

    assert keys, "no context keys were found"
    assert all(isinstance(key, str) and key for key in keys)
    assert len(keys) == len(set(keys))


async def test_gb_to_bytes_conversion():
    assert constants.GB_TO_B == 1024 * 1024 * 1024


async def test_known_sentinels_are_stable():
    assert constants.VM_XML_CONTEXT_KEY == "vm_xml"
    assert constants.NPU_LIST_CONTEXT_KEY == "NPU_LIST_CONTEXT_KEY"
