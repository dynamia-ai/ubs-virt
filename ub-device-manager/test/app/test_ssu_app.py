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
"""Unit tests for :mod:`ub_device_manager.app.ssu_app`.

Only the released SSU endpoints are covered: query (list/show), delete and
alloc.
"""
from unittest.mock import patch

import pytest
from fastapi.testclient import TestClient
from test.helpers import make_fake_chain

from ub_device_manager.app import ssu_app
from ub_device_manager.app.models import SsuInfo
from ub_device_manager.constants import (
    SSU_ALLOC_RESULT_CONTEXT_KEY,
    SSU_LIST_CONTEXT_KEY,
    SSU_SHOW_RESULT_CONTEXT_KEY,
)


@pytest.fixture
def client():
    return TestClient(ssu_app.app)


def ssu_info(name="vol"):
    return SsuInfo(name=name, strategy=2, namespace_cnt=0, namespaces=[])


ALLOC_BODY = {
    "name": "vol",
    "ns_size_gb": 1,
    "ns_num": 1,
    "lba_format": 512,
    "strategy": 2,
}


class TestQueryEndpoints:
    @patch.object(ssu_app, "AsyncTaskChain", make_fake_chain({SSU_LIST_CONTEXT_KEY: [ssu_info("a"), ssu_info("b")]}))
    async def test_list_ssu(self, client):
        response = client.get("/ssu")

        assert [item["name"] for item in response.json()] == ["a", "b"]

    @patch.object(ssu_app, "AsyncTaskChain", make_fake_chain({SSU_SHOW_RESULT_CONTEXT_KEY: ssu_info("vol")}))
    async def test_show_ssu(self, client):
        response = client.get("/ssu/vol")

        assert response.status_code == 200
        assert response.json()["name"] == "vol"


class TestAllocAndDeleteEndpoints:
    @patch.object(ssu_app, "AsyncTaskChain", make_fake_chain({SSU_ALLOC_RESULT_CONTEXT_KEY: ssu_info("vol")}))
    async def test_alloc(self, client):
        response = client.post("/ssu/alloc", json=ALLOC_BODY)

        assert response.status_code == 200
        assert response.json()["name"] == "vol"

    async def test_alloc_rejects_invalid_body(self, client):
        response = client.post("/ssu/alloc", json={"name": "vol", "ns_size_gb": 0, "ns_num": 1, "lba_format": 512, "strategy": 2})

        assert response.status_code == 422

    @patch.object(ssu_app, "AsyncTaskChain", make_fake_chain())
    async def test_free_ssu(self, client):
        response = client.delete("/ssu/vol")

        assert response.status_code == 200
        assert response.json()["msg"] == "Freed vol successfully."
