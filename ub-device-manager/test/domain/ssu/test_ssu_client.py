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
"""Unit tests for :mod:`ub_device_manager.domain.ssu.ssu_client`.

Only the released SSU client operations are covered: alloc, free, list and
show.
"""
from types import SimpleNamespace
from unittest.mock import patch

import pytest

from ub_device_manager.app.models import (
    SsuAllocSpaceReq,
    SsuAllocStrategy,
    SsuLbaFormat,
)
from ub_device_manager.constants import GB_TO_B
from ub_device_manager.domain.ssu.ssu_client import SsuClient
from ub_device_manager.exceptions import SsuNotFound


def make_ns_info(**overrides):
    data = {
        "tgt_eid": "eid",
        "tgt_nqn": "nqn",
        "ns_uuid": "uuid",
        "namespace_id": 1,
        "ns_dev_path": "/dev/nvme0n1",
        "ns_size": 1024,
        "lba_format": 512,
    }
    data.update(overrides)
    return SimpleNamespace(**data)


def make_alloc_result(name="vol", strategy=2, namespaces=None):
    return SimpleNamespace(name=name, strategy=strategy, namespaces=namespaces if namespaces is not None else [make_ns_info()])


class TestConversions:
    async def test_alloc_result_conversion(self):
        result = SsuClient._ubse_alloc_result_to_ssu_info(make_alloc_result())

        assert result.name == "vol"
        assert result.strategy == 2
        assert result.namespace_cnt == 1
        assert result.namespaces[0].ns_id == 1
        assert result.namespaces[0].lba_format == 512

    @pytest.mark.parametrize(
        ("tenant", "expected"),
        [(None, b""), ("", b""), ("tenant", b"tenant")],
    )
    async def test_tenant_to_bytes(self, tenant, expected):
        assert SsuClient._tenant_to_bytes(tenant) == expected


class TestAllocAndList:
    @patch("ubse.ubs_engine_ssu.ubs_ssu_space_alloc")
    async def test_ssu_alloc_builds_request_in_bytes(self, mock_alloc):
        mock_alloc.return_value = make_alloc_result()
        body = SsuAllocSpaceReq(
            name="vol",
            ns_size_gb=2,
            ns_num=4,
            lba_format=SsuLbaFormat.integer_512,
            strategy=SsuAllocStrategy.integer_1,
            tenant="tenant",
        )

        result = await SsuClient().ssu_alloc(body)

        assert result.name == "vol"
        request = mock_alloc.call_args.args[0]
        assert request.ns_size == 2 * GB_TO_B
        assert request.tenant == b"tenant"
        assert int(request.strategy) == 1

    @patch("ubse.ubs_engine_ssu.ubs_ssu_alloc_info_list")
    async def test_get_ssu_list(self, mock_list):
        mock_list.return_value = [make_alloc_result("a"), make_alloc_result("b")]

        result = await SsuClient().get_ssu_list()

        assert [item.name for item in result] == ["a", "b"]

    @patch("ubse.ubs_engine_ssu.ubs_ssu_alloc_info_list")
    async def test_show_ssu_alloc_info_found(self, mock_list):
        mock_list.return_value = [make_alloc_result("a"), make_alloc_result("b")]

        result = await SsuClient().show_ssu_alloc_info("b")

        assert result.name == "b"

    @patch("ubse.ubs_engine_ssu.ubs_ssu_alloc_info_list", return_value=[])
    async def test_show_ssu_alloc_info_not_found(self, mock_list):
        with pytest.raises(SsuNotFound, match="ssu not found"):
            await SsuClient().show_ssu_alloc_info("missing")

    @patch("ubse.ubs_engine_ssu.ubs_ssu_space_free")
    async def test_ssu_free(self, mock_free):
        await SsuClient().ssu_free("vol")

        mock_free.assert_called_once_with("vol")
