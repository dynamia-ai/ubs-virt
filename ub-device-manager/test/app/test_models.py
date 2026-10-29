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
"""Unit tests for :mod:`ub_device_manager.app.models` request/response validation."""
import pytest
from pydantic import ValidationError

from ub_device_manager.app.models import (
    BindNpuDeviceRequest,
    BindNpuDeviceResult,
    BindSsuVfeReq,
    BoundUbDevice,
    CreateVmRequest,
    Message,
    NpuDeviceInfo,
    SsuAllocSpaceReq,
    SsuAllocStrategy,
    SsuInfo,
    SsuLbaFormat,
    SsuPermReq,
    SsuSpaceReq,
    UnbindNpuDeviceRequest,
    UnbindSsuVfeReq,
    VfeForSsu,
)


class TestCreateVmRequest:
    @pytest.mark.parametrize("upi", ["0x0001", "0x0f", "0x0F", "0X0f", "0x0fff"])
    async def test_accepts_valid_upi(self, upi):
        request = CreateVmRequest(xml_text="<domain/>", upi=upi)
        assert request.upi == upi

    @pytest.mark.parametrize("upi", ["0x0000", "0x1000", "0f", "0x", "0xffff", "1"])
    async def test_rejects_invalid_upi(self, upi):
        with pytest.raises(ValidationError):
            CreateVmRequest(xml_text="<domain/>", upi=upi)

    @pytest.mark.parametrize("count", [1, 2, 4, 8])
    async def test_accepts_supported_npu_counts(self, count):
        assert CreateVmRequest(upi="0x1", npu_count=count).npu_count == count

    @pytest.mark.parametrize("count", [0, 3, 5, 16])
    async def test_rejects_unsupported_npu_counts(self, count):
        with pytest.raises(ValidationError):
            CreateVmRequest(upi="0x1", npu_count=count)

    async def test_defaults(self):
        request = CreateVmRequest(upi="0x1")
        assert request.xml_text is None
        assert request.need_nic is False
        assert request.npu_ids is None
        assert request.npu_count is None


class TestBindNpuDeviceRequest:
    async def test_defaults(self):
        request = BindNpuDeviceRequest(upi="0x1")
        assert request.ids is None
        assert request.count is None
        assert request.need_nic is False

    @pytest.mark.parametrize("count", [1, 2, 4, 8])
    async def test_accepts_supported_counts(self, count):
        assert BindNpuDeviceRequest(upi="0x1", count=count).count == count

    @pytest.mark.parametrize("count", [0, 3, 7])
    async def test_rejects_unsupported_counts(self, count):
        with pytest.raises(ValidationError):
            BindNpuDeviceRequest(upi="0x1", count=count)


class TestUnbindNpuDeviceRequest:
    async def test_bus_guid_is_required(self):
        with pytest.raises(ValidationError):
            UnbindNpuDeviceRequest()

    async def test_accepts_guid_within_max_length(self):
        assert UnbindNpuDeviceRequest(bus_guid="g" * 128).bus_guid == "g" * 128

    async def test_rejects_guid_over_max_length(self):
        with pytest.raises(ValidationError):
            UnbindNpuDeviceRequest(bus_guid="g" * 129)


class TestSsuModels:
    async def test_alloc_request_requires_format_and_strategy(self):
        with pytest.raises(ValidationError):
            SsuAllocSpaceReq(name="vol", ns_size_gb=1, ns_num=1)

    async def test_alloc_request_defaults_tenant_to_empty(self):
        request = SsuAllocSpaceReq(
            name="vol",
            ns_size_gb=1,
            ns_num=1,
            lba_format=SsuLbaFormat.integer_512,
            strategy=SsuAllocStrategy.integer_2,
        )

        assert request.lba_format == SsuLbaFormat.integer_512
        assert request.strategy == SsuAllocStrategy.integer_2
        assert request.tenant == ""

    @pytest.mark.parametrize(
        ("field", "value"),
        [
            ("name", ""),
            ("name", "x" * 49),
            ("ns_size_gb", 0),
            ("ns_num", 0),
        ],
    )
    async def test_alloc_request_rejects_out_of_range_values(self, field, value):
        payload = {
            "name": "vol",
            "ns_size_gb": 1,
            "ns_num": 1,
            "lba_format": SsuLbaFormat.integer_512,
            "strategy": SsuAllocStrategy.integer_2,
            field: value,
        }

        with pytest.raises(ValidationError):
            SsuAllocSpaceReq(**payload)

    @pytest.mark.parametrize("tenant", ["", "tenant-1", "a.b:c_d"])
    async def test_tenant_accepts_allowed_characters(self, tenant):
        request = SsuAllocSpaceReq(
            name="vol",
            ns_size_gb=1,
            ns_num=1,
            lba_format=SsuLbaFormat.integer_512,
            strategy=SsuAllocStrategy.integer_2,
            tenant=tenant,
        )
        assert request.tenant == tenant

    @pytest.mark.parametrize("tenant", ["bad space", "semi;colon", "at@sign"])
    async def test_tenant_rejects_disallowed_characters(self, tenant):
        with pytest.raises(ValidationError):
            SsuAllocSpaceReq(
                name="vol",
                ns_size_gb=1,
                ns_num=1,
                lba_format=SsuLbaFormat.integer_512,
                strategy=SsuAllocStrategy.integer_2,
                tenant=tenant,
            )

    async def test_space_request_optional_fields(self):
        request = SsuSpaceReq(name="vol")

        assert request.nqn == ""
        assert request.src_eid == ""

    async def test_space_request_length_limits(self):
        with pytest.raises(ValidationError):
            SsuSpaceReq(name="vol", nqn="n" * 70)

        with pytest.raises(ValidationError):
            SsuSpaceReq(name="vol", src_eid="e" * 18)

    async def test_perm_request_requires_name_and_nqn(self):
        with pytest.raises(ValidationError):
            SsuPermReq(name="vol")

        assert SsuPermReq(name="vol", nqn="nqn.x").nqn == "nqn.x"

    async def test_bind_vfe_allows_empty_bus_guid(self):
        request = BindSsuVfeReq(upi=1, vfe_guid="g", bus_guid="")
        assert request.bus_guid == ""

    async def test_bind_vfe_rejects_bus_guid_over_32_chars(self):
        with pytest.raises(ValidationError):
            BindSsuVfeReq(upi=1, vfe_guid="g", bus_guid="x" * 33)

    async def test_unbind_vfe_requires_upi_and_guid(self):
        with pytest.raises(ValidationError):
            UnbindSsuVfeReq(upi=1)

    async def test_vfe_for_ssu_bounds(self):
        with pytest.raises(ValidationError):
            VfeForSsu(slot_id=256)

        assert VfeForSsu(slot_id=255).slot_id == 255

    async def test_ssu_info_namespace_count(self):
        info = SsuInfo(name="vol", strategy=1, namespace_cnt=0, namespaces=[])
        assert info.namespace_cnt == 0


class TestDeviceModels:
    async def test_bound_device_length_limits(self):
        assert BoundUbDevice(id="1-1", guid="g", type="NPU").type == "NPU"

        with pytest.raises(ValidationError):
            BoundUbDevice(id="x" * 65, guid="g", type="NPU")

    async def test_npu_device_info_optional_bus_guid(self):
        assert NpuDeviceInfo(id="0-1", guid="g").bus_guid is None

    async def test_bind_result_requires_devices(self):
        result = BindNpuDeviceResult(
            tid=1,
            uba=2,
            size=3,
            bus_guid="bus",
            devices=[BoundUbDevice(id="1-1", guid="g", type="NPU")],
        )
        assert result.devices[0].id == "1-1"

    async def test_message_max_length(self):
        assert Message(msg="ok").msg == "ok"

        with pytest.raises(ValidationError):
            Message(msg="x" * 257)
