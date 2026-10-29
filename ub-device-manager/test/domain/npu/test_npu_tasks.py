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
"""Unit tests for :mod:`ub_device_manager.domain.npu.npu_tasks`."""
from unittest.mock import AsyncMock, patch

import pytest
from test.helpers import run_task

from ub_device_manager.app.models import BindNpuDeviceRequest
from ub_device_manager.constants import (
    BIND_REQUEST_CONTEXT_KEY,
    BIND_RESULT_CONTEXT_KEY,
    BOUND_DEVICES_CONTEXT_KEY,
    NPU_LIST_CONTEXT_KEY,
    UNBIND_REQUEST_CONTEXT_KEY,
)
from ub_device_manager.domain.npu.npu_client import NpuClient, UbDeviceInfo
from ub_device_manager.domain.npu.npu_tasks import (
    ALLOWED_DEVICE_COUNTS,
    BindNpuDeviceTask,
    QueryNpuTask,
    UnbindNpuDeviceTask,
)
from ub_device_manager.exceptions import (
    InvalidBindDeviceRequest,
    ResolveNpuDevicesFailed,
    UnbindDeviceFailed,
    UnbindDeviceNotFound,
)


def device(device_id, device_type="NPU", bus_guid=None):
    return UbDeviceInfo(id=device_id, type=device_type, guid=f"guid-{device_id}", bus_guid=bus_guid)


class TestQueryNpuTask:
    @patch.object(NpuClient, "get_host_ub_devices", new_callable=AsyncMock)
    async def test_keeps_only_npu_devices(self, mock_get):
        mock_get.return_value = [device("0-1"), device("1-1", "NIC_VFE")]

        context = await run_task(QueryNpuTask())

        assert [item.id for item in context.get(NPU_LIST_CONTEXT_KEY)] == ["0-1"]


class TestBindNpuDeviceTaskShouldRun:
    async def test_skips_when_no_bind_request(self):
        assert await BindNpuDeviceTask().should_run() is False

    async def test_runs_when_bind_request_present(self):
        task = BindNpuDeviceTask()
        task.context.set(BIND_REQUEST_CONTEXT_KEY, BindNpuDeviceRequest(upi="0x1", count=1))

        assert await task.should_run() is True


class TestBindNpuDeviceRequestValidation:
    @pytest.mark.parametrize(
        ("payload", "message"),
        [
            (BindNpuDeviceRequest.model_construct(upi="zz", ids=None, count=1, need_nic=False), "hexadecimal"),
            (BindNpuDeviceRequest.model_construct(upi="0x0", ids=None, count=1, need_nic=False), "range"),
            (BindNpuDeviceRequest(upi="0x1"), "exactly one"),
            (BindNpuDeviceRequest(upi="0x1", ids=["1-1"], count=1), "exactly one"),
            (BindNpuDeviceRequest(upi="0x1", ids=["1-1", "1-1"]), "repetitive"),
            (BindNpuDeviceRequest.model_construct(upi="0x1", ids=["1-1", "1-2", "1-3"], count=None, need_nic=False), "1/2/4/8"),
        ],
    )
    async def test_invalid_requests_are_rejected(self, payload, message):
        with pytest.raises(InvalidBindDeviceRequest, match=message):
            BindNpuDeviceTask._validate_request(payload)

    async def test_allowed_counts_are_stable(self):
        assert ALLOWED_DEVICE_COUNTS == {1, 2, 4, 8}


class TestBindNpuDeviceTaskExecute:
    @patch.object(NpuClient, "get_host_ub_devices", new_callable=AsyncMock)
    @patch.object(NpuClient, "bind_devices", new_callable=AsyncMock)
    async def test_binds_explicit_device_and_sets_context(self, mock_bind, mock_get):
        mock_get.return_value = [device("0-1")]
        mock_bind.return_value = ("bus-1", 1, 2, 3)

        context = await run_task(
            BindNpuDeviceTask(),
            {BIND_REQUEST_CONTEXT_KEY: BindNpuDeviceRequest(upi="0x0f", ids=["0-1"])},
        )

        params = mock_bind.call_args.args[0]
        assert params.upi == "0x0f"
        assert [(item.device_id, item.device_type) for item in params.device_list] == [("0-1", "NPU")]
        assert context.get(BOUND_DEVICES_CONTEXT_KEY) == [{"id": "0-1", "guid": "guid-0-1", "type": "NPU"}]
        assert context.get(BIND_RESULT_CONTEXT_KEY).bus_guid == "bus-1"

    @patch.object(NpuClient, "get_host_ub_devices", new_callable=AsyncMock)
    @patch.object(NpuClient, "bind_devices", new_callable=AsyncMock)
    async def test_binds_by_count(self, mock_bind, mock_get):
        mock_get.return_value = [device("0-1"), device("0-2")]
        mock_bind.return_value = ("bus-1", 1, 2, 3)

        await run_task(
            BindNpuDeviceTask(),
            {BIND_REQUEST_CONTEXT_KEY: BindNpuDeviceRequest(upi="0x1", count=2)},
        )

        params = mock_bind.call_args.args[0]
        assert [item.device_id for item in params.device_list] == ["0-1", "0-2"]

    @patch.object(NpuClient, "get_host_ub_devices", new_callable=AsyncMock)
    async def test_resolution_failure_is_wrapped(self, mock_get):
        mock_get.return_value = [device("0-1")]

        with pytest.raises(ResolveNpuDevicesFailed):
            await run_task(
                BindNpuDeviceTask(),
                {BIND_REQUEST_CONTEXT_KEY: BindNpuDeviceRequest(upi="0x1", ids=["9-9"])},
            )


class TestUnbindNpuDeviceTaskShouldRun:
    async def test_skips_when_bus_guid_absent(self):
        assert await UnbindNpuDeviceTask().should_run() is False

    async def test_skips_when_bus_guid_empty(self):
        task = UnbindNpuDeviceTask()
        task.context.set(UNBIND_REQUEST_CONTEXT_KEY, "")

        assert await task.should_run() is False

    async def test_runs_when_bus_guid_present(self):
        task = UnbindNpuDeviceTask()
        task.context.set(UNBIND_REQUEST_CONTEXT_KEY, "bus-1")

        assert await task.should_run() is True


class TestUnbindNpuDeviceTaskExecute:
    @patch.object(NpuClient, "get_host_ub_devices", new_callable=AsyncMock)
    @patch.object(NpuClient, "free_devices", new_callable=AsyncMock)
    async def test_frees_devices_bound_to_bus_guid(self, mock_free, mock_get):
        mock_get.return_value = [device("0-1", bus_guid="bus-1"), device("0-2")]

        await run_task(UnbindNpuDeviceTask(), {UNBIND_REQUEST_CONTEXT_KEY: "bus-1"})

        mock_free.assert_called_once_with("bus-1", [{"device_id": "0-1", "device_type": "NPU"}])

    @patch.object(NpuClient, "get_host_ub_devices", new_callable=AsyncMock, return_value=[])
    async def test_no_bound_devices_raises(self, mock_get):
        with pytest.raises(UnbindDeviceNotFound):
            await run_task(UnbindNpuDeviceTask(), {UNBIND_REQUEST_CONTEXT_KEY: "bus-1"})

    @patch.object(NpuClient, "free_devices", new_callable=AsyncMock, side_effect=RuntimeError("free failed"))
    @patch.object(NpuClient, "get_host_ub_devices", new_callable=AsyncMock)
    async def test_free_failure_is_wrapped(self, mock_get, mock_free):
        mock_get.return_value = [device("0-1", bus_guid="bus-1")]

        with pytest.raises(UnbindDeviceFailed):
            await run_task(UnbindNpuDeviceTask(), {UNBIND_REQUEST_CONTEXT_KEY: "bus-1"})
