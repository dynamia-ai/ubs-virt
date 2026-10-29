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
"""Unit tests for :mod:`ub_device_manager.domain.ssu.ssu_tasks`.

Only the released SSU tasks are covered: alloc, list, show and free.
"""
from unittest.mock import AsyncMock, patch

import pytest
from test.helpers import run_task

from ub_device_manager.app.models import (
    SsuAllocSpaceReq,
    SsuAllocStrategy,
    SsuInfo,
    SsuLbaFormat,
)
from ub_device_manager.constants import (
    SSU_ALLOC_REQUEST_CONTEXT_KEY,
    SSU_ALLOC_RESULT_CONTEXT_KEY,
    SSU_FREE_REQUEST_CONTEXT_KEY,
    SSU_LIST_CONTEXT_KEY,
    SSU_SHOW_REQUEST_CONTEXT_KEY,
    SSU_SHOW_RESULT_CONTEXT_KEY,
)
from ub_device_manager.domain.ssu.ssu_client import SsuClient
from ub_device_manager.domain.ssu.ssu_tasks import (
    AllocSsuTask,
    FreeSsuSpaceTask,
    GetSsuListTask,
    ShowSsuTask,
)
from ub_device_manager.exceptions import InvalidSsuRequest


def ssu_info(name="vol"):
    return SsuInfo(name=name, strategy=2, namespace_cnt=0, namespaces=[])


def make_alloc_request(**overrides):
    payload = {
        "name": "vol",
        "ns_size_gb": 1,
        "ns_num": 1,
        "lba_format": SsuLbaFormat.integer_512,
        "strategy": SsuAllocStrategy.integer_2,
    }
    payload.update(overrides)
    return SsuAllocSpaceReq(**payload)


class TestAllocSsuTask:
    async def test_validation_rejects_small_size(self):
        request = SsuAllocSpaceReq.model_construct(
            name="vol", ns_size_gb=0, ns_num=1, lba_format=SsuLbaFormat.integer_512, strategy=SsuAllocStrategy.integer_2, tenant=""
        )

        with pytest.raises(InvalidSsuRequest, match="greater than or equal to 1"):
            AllocSsuTask._validate_request(request)

    async def test_validation_rejects_striped_single_namespace(self):
        request = make_alloc_request(ns_num=1, strategy=SsuAllocStrategy.integer_0)

        with pytest.raises(InvalidSsuRequest, match="STRIPED"):
            AllocSsuTask._validate_request(request)

    @patch.object(SsuClient, "ssu_alloc", new_callable=AsyncMock)
    async def test_execute_stores_result_in_context(self, mock_alloc):
        mock_alloc.return_value = ssu_info("vol")

        context = await run_task(AllocSsuTask(), {SSU_ALLOC_REQUEST_CONTEXT_KEY: make_alloc_request()})

        assert context.get(SSU_ALLOC_RESULT_CONTEXT_KEY).name == "vol"

    @patch.object(SsuClient, "ssu_alloc", new_callable=AsyncMock)
    async def test_execute_rejects_invalid_request(self, mock_alloc):
        request = make_alloc_request(ns_num=1, strategy=SsuAllocStrategy.integer_0)

        with pytest.raises(InvalidSsuRequest):
            await run_task(AllocSsuTask(), {SSU_ALLOC_REQUEST_CONTEXT_KEY: request})

        mock_alloc.assert_not_called()


class TestQueryAndFreeTasks:
    @patch.object(SsuClient, "get_ssu_list", new_callable=AsyncMock)
    async def test_get_ssu_list(self, mock_list):
        mock_list.return_value = [ssu_info("a")]

        context = await run_task(GetSsuListTask())

        assert [item.name for item in context.get(SSU_LIST_CONTEXT_KEY)] == ["a"]

    @patch.object(SsuClient, "show_ssu_alloc_info", new_callable=AsyncMock)
    async def test_show_ssu(self, mock_show):
        mock_show.return_value = ssu_info("vol")

        context = await run_task(ShowSsuTask(), {SSU_SHOW_REQUEST_CONTEXT_KEY: "vol"})

        assert context.get(SSU_SHOW_RESULT_CONTEXT_KEY).name == "vol"

    @patch.object(SsuClient, "ssu_free", new_callable=AsyncMock)
    async def test_free_ssu(self, mock_free):
        await run_task(FreeSsuSpaceTask(), {SSU_FREE_REQUEST_CONTEXT_KEY: "vol"})

        mock_free.assert_called_once_with("vol")
