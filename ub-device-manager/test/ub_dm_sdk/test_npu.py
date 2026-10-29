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
"""Unit tests for :mod:`ub_device_manager.ub_dm_sdk.npu`."""
from unittest.mock import patch

from ub_device_manager.ub_dm_sdk.http_client import UbDeviceManagerClient
from ub_device_manager.ub_dm_sdk.models import BoundUbDevice, NpuDeviceInfo
from ub_device_manager.ub_dm_sdk.npu import bind_npu_device, get_host_npu_device, unbind_npu_device


class TestGetHostNpuDevice:
    @patch.object(UbDeviceManagerClient, "request")
    async def test_parses_device_list(self, mock_request):
        mock_request.return_value = [{"id": "0-1", "guid": "g1"}, {"id": "0-2", "guid": "g2", "bus_guid": "b"}]

        devices = get_host_npu_device()

        assert devices == [
            NpuDeviceInfo(id="0-1", guid="g1"),
            NpuDeviceInfo(id="0-2", guid="g2", bus_guid="b"),
        ]
        mock_request.assert_called_once_with("GET", "/npu")

    @patch.object(UbDeviceManagerClient, "request", return_value=None)
    async def test_empty_response_yields_empty_list(self, mock_request):
        assert get_host_npu_device() == []


class TestBindNpuDevice:
    @patch.object(UbDeviceManagerClient, "post")
    async def test_returns_tuple_and_parses_devices(self, mock_post):
        mock_post.return_value = {
            "tid": 1,
            "uba": 2,
            "size": 3,
            "bus_guid": "bus",
            "devices": [{"id": "1-1", "guid": "g", "type": "NPU"}],
        }

        tid, uba, size, bus_guid, devices = bind_npu_device("0x1", count=1)

        assert (tid, uba, size, bus_guid) == (1, 2, 3, "bus")
        assert devices == [BoundUbDevice(id="1-1", guid="g", type="NPU")]

    @patch.object(UbDeviceManagerClient, "post", return_value={})
    async def test_request_payload_excludes_none_fields(self, mock_post):
        bind_npu_device("0x1", ids=["1-1"], need_nic=True)

        mock_post.assert_called_once_with(
            "/npu/bind",
            json={"upi": "0x1", "ids": ["1-1"], "need_nic": True},
        )

    @patch.object(UbDeviceManagerClient, "post", return_value=None)
    async def test_missing_response_defaults_to_empty_devices(self, mock_post):
        tid, uba, size, bus_guid, devices = bind_npu_device("0x1", count=1)

        assert (tid, uba, size, bus_guid, devices) == (None, None, None, None, [])


class TestUnbindNpuDevice:
    @patch.object(UbDeviceManagerClient, "post")
    async def test_posts_bus_guid(self, mock_post):
        unbind_npu_device("bus-1")

        mock_post.assert_called_once_with("/npu/unbind", json={"bus_guid": "bus-1"})
