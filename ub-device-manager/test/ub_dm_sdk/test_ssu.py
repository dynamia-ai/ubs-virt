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
"""Unit tests for :mod:`ub_device_manager.ub_dm_sdk.ssu`."""
from unittest.mock import patch

from ub_device_manager.ub_dm_sdk.http_client import UbDeviceManagerClient
from ub_device_manager.ub_dm_sdk.models import SsuAllocStrategy, SsuInfo, SsuLbaFormat
from ub_device_manager.ub_dm_sdk.ssu import alloc_ssu, free_ssu, get_ssu, get_ssu_list

SSU_PAYLOAD = {"name": "vol", "strategy": 2, "namespace_cnt": 0, "namespaces": []}


class TestGetSsuList:
    @patch.object(UbDeviceManagerClient, "request")
    async def test_parses_list(self, mock_request):
        mock_request.return_value = [SSU_PAYLOAD]

        result = get_ssu_list()

        assert result == [SsuInfo(**SSU_PAYLOAD)]
        mock_request.assert_called_once_with("GET", "/ssu")

    @patch.object(UbDeviceManagerClient, "request", return_value=None)
    async def test_none_response_yields_empty_list(self, mock_request):
        assert get_ssu_list() == []


class TestGetSsu:
    @patch.object(UbDeviceManagerClient, "request", return_value=SSU_PAYLOAD)
    async def test_parses_single_item(self, mock_request):
        result = get_ssu("vol")

        assert result.name == "vol"
        mock_request.assert_called_once_with("GET", "/ssu/vol")


class TestAllocSsu:
    @patch.object(UbDeviceManagerClient, "post")
    async def test_posts_expected_payload(self, mock_post):
        mock_post.return_value = SSU_PAYLOAD

        result = alloc_ssu(
            "vol",
            ns_size_gb=10,
            ns_num=2,
            lba_format=SsuLbaFormat.FORMAT_4K,
            strategy=SsuAllocStrategy.LINEAR,
            tenant="tenant-1",
        )

        assert result.name == "vol"
        mock_post.assert_called_once_with(
            "/ssu/alloc",
            json={
                "name": "vol",
                "ns_size_gb": 10,
                "ns_num": 2,
                "lba_format": 4096,
                "strategy": 1,
                "tenant": "tenant-1",
            },
        )

    @patch.object(UbDeviceManagerClient, "post")
    async def test_excludes_none_tenant(self, mock_post):
        mock_post.return_value = SSU_PAYLOAD

        alloc_ssu("vol", ns_size_gb=1, ns_num=1)

        assert "tenant" not in mock_post.call_args.kwargs["json"]


class TestFreeSsu:
    @patch.object(UbDeviceManagerClient, "request")
    async def test_deletes_by_name(self, mock_request):
        free_ssu("vol")

        mock_request.assert_called_once_with("DELETE", "/ssu/vol")
