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
"""Unit tests for :mod:`ub_device_manager.app.vm_app`."""
from unittest.mock import patch

import pytest
from fastapi.testclient import TestClient
from test.helpers import make_fake_chain

from ub_device_manager.app import vm_app


@pytest.fixture
def client():
    return TestClient(vm_app.app)


class TestCreateVm:
    @patch.object(vm_app, "AsyncTaskChain", make_fake_chain())
    async def test_success(self, client):
        response = client.post("/vm/create", json={"upi": "0x1", "xml_text": "<domain/>"})

        assert response.status_code == 200
        assert response.json()["msg"] == "Create VM successfully"

    async def test_invalid_upi_returns_422(self, client):
        response = client.post("/vm/create", json={"upi": "0x0000", "xml_text": "<domain/>"})

        assert response.status_code == 422


class TestDeleteVm:
    @patch.object(vm_app, "AsyncTaskChain", make_fake_chain())
    async def test_success(self, client):
        response = client.delete("/vm/vm-1")

        assert response.status_code == 200
        assert response.json()["msg"] == "Deleted vm-1 successfully."
