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
"""Unit tests for :mod:`ub_device_manager.ub_dm_sdk.models`."""
import pytest
from pydantic import ValidationError

from ub_device_manager.ub_dm_sdk.models import (
    CreateVmRequest,
    SsuAllocSpaceReq,
    SsuAllocStrategy,
    SsuInfo,
    SsuLbaFormat,
)


class TestCreateVmRequest:
    async def test_upi_is_required(self):
        with pytest.raises(ValidationError):
            CreateVmRequest()

    async def test_extra_fields_are_forbidden(self):
        with pytest.raises(ValidationError):
            CreateVmRequest(upi="0x1", unexpected=1)

    async def test_optional_fields_default(self):
        request = CreateVmRequest(upi="0x1")

        assert request.xml_text is None
        assert request.need_nic is False
        assert request.npu_count is None

    async def test_negative_npu_count_is_rejected(self):
        with pytest.raises(ValidationError):
            CreateVmRequest(upi="0x1", npu_count=-1)


class TestSsuModels:
    async def test_alloc_request_validation(self):
        with pytest.raises(ValidationError):
            SsuAllocSpaceReq(name="vol", ns_size_gb=0, ns_num=1)

        with pytest.raises(ValidationError):
            SsuAllocSpaceReq(name="", ns_size_gb=1, ns_num=1)

    async def test_alloc_request_defaults(self):
        request = SsuAllocSpaceReq(name="vol", ns_size_gb=1, ns_num=1)

        assert request.lba_format == SsuLbaFormat.FORMAT_512
        assert request.strategy == SsuAllocStrategy.NORMAL
        assert request.tenant is None

    async def test_enum_values(self):
        assert SsuLbaFormat.FORMAT_512 == 512
        assert SsuLbaFormat.FORMAT_4K == 4096
        assert SsuAllocStrategy.STRIPED == 0
        assert SsuAllocStrategy.NORMAL == 2

    async def test_ssu_info_defaults_namespaces(self):
        info = SsuInfo(name="vol", strategy=1, namespace_cnt=0)

        assert info.namespaces == []
