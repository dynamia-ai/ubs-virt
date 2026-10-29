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
"""Unit tests for :mod:`ub_device_manager.domain.npu.npu_client`."""
from unittest.mock import patch

import pytest

from ub_device_manager.domain.npu import npu_client as npu_client_module
from ub_device_manager.domain.npu.npu_client import (
    AffinityDeviceInfo,
    AllocDevicesParams,
    DeviceRef,
    NpuClient,
    UbDeviceInfo,
)


class FakeHostDevice:
    def __init__(self, device_type, device_id, guid, bus_instance=None, affinity_devs=None):
        self.device_type = device_type
        self.device_id = device_id
        self.guid = guid
        self.bus_instance = bus_instance
        self.affinity_devs = affinity_devs or []


class TestLogCallbackRegistration:
    @patch.object(npu_client_module, "_ubse_log_registered", False)
    @patch("ubse.ubs_engine_log.ubs_engine_log_callback_register")
    async def test_callback_is_registered_only_once(self, mock_register):
        npu_client_module._register_ubse_log_callback()
        npu_client_module._register_ubse_log_callback()

        assert mock_register.call_count == 1


class TestGetHostUbDevices:
    @patch("ubse.ubs_engine_npu.get_host_ub_devices")
    async def test_filters_and_converts_inventory(self, mock_get):
        mock_get.return_value = [
            FakeHostDevice("NPU", "0-1", "g1", bus_instance="bus", affinity_devs=[]),
            FakeHostDevice("NIC_VFE", "1-1", "g2"),
            FakeHostDevice("GPU", "2-1", "g3"),
        ]

        devices = await NpuClient.get_host_ub_devices()

        assert [device.id for device in devices] == ["0-1", "1-1"]
        assert devices[0].bus_guid == "bus"
        assert devices[0].type == "NPU"


class TestBindDevices:
    def _params(self):
        return AllocDevicesParams(
            upi="0x0f",
            device_list=[DeviceRef(device_id="1-1", device_type="NPU")],
        )

    @patch("ubse.ubs_engine_npu.query_uba_tid_size", return_value=(11, 22, 33))
    @patch("ubse.ubs_engine_npu.alloc_devices")
    async def test_strips_hex_prefix_and_returns_mapping(self, mock_alloc, mock_query):
        mock_alloc.return_value = ("bus-guid", None)

        result = await NpuClient().bind_devices(self._params())

        assert result == ("bus-guid", 11, 22, 33)
        mock_alloc.assert_called_once_with(
            upi="0f",
            bus_guid="",
            device_list=[{"device_id": "1-1", "device_type": "NPU"}],
        )

    @patch("ubse.ubs_engine_npu.query_uba_tid_size", return_value=(1, 2, 3))
    @patch("ubse.ubs_engine_npu.alloc_devices", return_value=("bus", None))
    async def test_keeps_non_prefixed_upi(self, mock_alloc, mock_query):
        await NpuClient().bind_devices(
            AllocDevicesParams(upi="0f", device_list=[DeviceRef(device_id="1-1", device_type="NPU")])
        )

        assert mock_alloc.call_args.kwargs["upi"] == "0f"

    @patch("ubse.ubs_engine_npu.free_devices")
    @patch("ubse.ubs_engine_npu.query_uba_tid_size", side_effect=RuntimeError("query failed"))
    @patch("ubse.ubs_engine_npu.alloc_devices", return_value=("bus-guid", None))
    async def test_releases_devices_when_mapping_query_fails(self, mock_alloc, mock_query, mock_free):
        with pytest.raises(RuntimeError, match="query failed"):
            await NpuClient().bind_devices(self._params())

        mock_free.assert_called_once_with(
            bus_instance_guid="bus-guid",
            device_list=[{"device_id": "1-1", "device_type": "NPU"}],
        )


class TestFreeDevices:
    @patch("ubse.ubs_engine_npu.free_devices")
    async def test_forwards_to_ubse(self, mock_free):
        await NpuClient.free_devices("bus", [{"device_id": "1-1", "device_type": "NPU"}])

        mock_free.assert_called_once_with(
            bus_instance_guid="bus",
            device_list=[{"device_id": "1-1", "device_type": "NPU"}],
        )


async def test_affinity_device_info_defaults():
    info = AffinityDeviceInfo(device_id="1-1", device_type="NIC_VFE")
    assert info.device_id == "1-1"


async def test_ub_device_info_requires_guid():
    assert UbDeviceInfo(id="0-1", type="NPU", guid="g").bus_guid is None
