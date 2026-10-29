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
"""Unit tests for :mod:`ub_device_manager.app.npu_app`."""
from unittest.mock import patch

import pytest
from fastapi.testclient import TestClient
from test.helpers import make_fake_chain

from ub_device_manager.app import npu_app
from ub_device_manager.app.models import BindNpuDeviceResult, BoundUbDevice, NpuDeviceInfo
from ub_device_manager.constants import BIND_RESULT_CONTEXT_KEY, NPU_LIST_CONTEXT_KEY


@pytest.fixture
def client():
    return TestClient(npu_app.app)


def bind_result_payload():
    return BindNpuDeviceResult(
        tid=1,
        uba=2,
        size=3,
        bus_guid="bus-1",
        devices=[BoundUbDevice(id="1-1", guid="g", type="NPU")],
    )


class TestGetNpuList:
    @patch.object(npu_app, "AsyncTaskChain", make_fake_chain({NPU_LIST_CONTEXT_KEY: [NpuDeviceInfo(id="0-1", guid="g")]}))
    async def test_returns_devices(self, client):
        response = client.get("/npu")

        assert response.status_code == 200
        assert response.json() == [{"id": "0-1", "guid": "g", "bus_guid": None}]


class TestBindNpuDevice:
    @patch.object(npu_app, "AsyncTaskChain", make_fake_chain({BIND_RESULT_CONTEXT_KEY: bind_result_payload()}))
    async def test_returns_bind_result(self, client):
        response = client.post("/npu/bind", json={"upi": "0x0f", "count": 1})

        assert response.status_code == 200
        assert response.json()["bus_guid"] == "bus-1"
        assert response.json()["devices"][0]["id"] == "1-1"

    async def test_invalid_upi_returns_422(self, client):
        response = client.post("/npu/bind", json={"upi": "not-a-upi", "count": 1})

        assert response.status_code == 422


class TestUnbindNpuDevice:
    async def test_empty_bus_guid_is_rejected(self, client):
        response = client.post("/npu/unbind", json={"bus_guid": "  "})

        assert response.status_code == 400
        assert response.json() == {"detail": "bus_guid is required"}

    @patch.object(npu_app, "AsyncTaskChain", make_fake_chain())
    async def test_success(self, client):
        response = client.post("/npu/unbind", json={"bus_guid": "bus-1"})

        assert response.status_code == 200
        assert response.json()["msg"] == "Unbind NPU devices successfully"
