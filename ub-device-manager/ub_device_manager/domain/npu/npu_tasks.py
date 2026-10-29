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
from __future__ import annotations

import re

from loguru import logger

from ub_device_manager.app.models import (
    BindNpuDeviceRequest,
    BindNpuDeviceResult,
    BoundUbDevice,
    NpuDeviceInfo,
)
from ub_device_manager.common.async_task_framework import AsyncTask
from ub_device_manager.constants import (
    BIND_REQUEST_CONTEXT_KEY,
    BIND_RESULT_CONTEXT_KEY,
    BOUND_DEVICES_CONTEXT_KEY,
    NPU_LIST_CONTEXT_KEY,
    UNBIND_REQUEST_CONTEXT_KEY,
)
from ub_device_manager.exceptions import (
    ResolveNpuDevicesFailed,
    InvalidBindDeviceRequest,
    UnbindDeviceFailed,
    UnbindDeviceNotFound,
)
from ub_device_manager.domain.npu.npu_client import AllocDevicesParams, NpuClient
from ub_device_manager.domain.npu.device_selector import UbDeviceSelector, UbDeviceType

# The UPI prefix and hexadecimal digits are case-insensitive; short forms are accepted.
UPI_PATTERN = re.compile(r"^0[xX][0-9a-fA-F]{1,4}$")
UPI_MAX = 0x0FFF
UPI_MIN = 0x0001
# A single NPU binding request may contain only 1, 2, 4, or 8 devices.
ALLOWED_DEVICE_COUNTS = {1, 2, 4, 8}


class QueryNpuTask(AsyncTask):

    def __init__(self):
        self.npu_client = NpuClient()

    async def execute(self) -> None:
        ub_devices = await self.npu_client.get_host_ub_devices()
        self.context.set(NPU_LIST_CONTEXT_KEY, [
            NpuDeviceInfo(id=d.id, guid=d.guid, bus_guid=d.bus_guid)
            for d in ub_devices
            if d.type == UbDeviceType.NPU
        ])


class BindNpuDeviceTask(AsyncTask):

    def __init__(self):
        self.npu_client = NpuClient()
        self.device_selector = UbDeviceSelector()

    async def should_run(self) -> bool:
        if not self.context.get(BIND_REQUEST_CONTEXT_KEY):
            logger.info("No UB devices need to bind.")
            return False
        return True

    @staticmethod
    def _validate_request(request: BindNpuDeviceRequest) -> None:
        if not UPI_PATTERN.fullmatch(request.upi):
            raise InvalidBindDeviceRequest(
                "upi must be a hexadecimal string with 1~4 digits, for example 0x000f or 0x0f"
            )
        if not UPI_MIN <= int(request.upi, 16) <= UPI_MAX:
            raise InvalidBindDeviceRequest("upi must be in range 0x0001~0x0fff")
        # Exactly one of ids and count must be provided.
        if bool(request.ids) == bool(request.count):
            raise InvalidBindDeviceRequest("exactly one of ids and count is required")
        if request.ids and len(set(request.ids)) != len(request.ids):
            raise InvalidBindDeviceRequest("repetitive device ids are not allowed")
        requested_count = len(request.ids) if request.ids else request.count
        if requested_count not in ALLOWED_DEVICE_COUNTS:
            raise InvalidBindDeviceRequest(
                f"requested device count must be one of 1/2/4/8, got {requested_count}"
            )

    async def execute(self) -> None:
        request: BindNpuDeviceRequest = self.context.get(BIND_REQUEST_CONTEXT_KEY)
        logger.debug("Bind NPU request: upi={}, ids={}, count={}, need_nic={}",
                     request.upi, request.ids, request.count, request.need_nic)
        self._validate_request(request)
        inventory = await self.npu_client.get_host_ub_devices()
        try:
            devices = self.device_selector.resolve_npu_devices(inventory, ids=request.ids, count=request.count,
                                                               need_nic=request.need_nic
                                                               )
        except ValueError as exc:
            raise ResolveNpuDevicesFailed(str(exc)) from exc
        device_list = [
            {"device_id": device.id, "device_type": device.type}
            for device in devices
        ]
        logger.debug("Resolved devices: {}", device_list)
        # bind_devices allocates devices and retrieves memory mapping data atomically, rolling back on failure.
        bus_guid, tid, uba, size = await self.npu_client.bind_devices(
            AllocDevicesParams(upi=request.upi, device_list=device_list)
        )
        self.context.set(BOUND_DEVICES_CONTEXT_KEY, [device.model_dump() for device in devices])
        self.context.set(BIND_RESULT_CONTEXT_KEY, BindNpuDeviceResult(tid=tid, uba=uba, size=size, bus_guid=bus_guid,
                                                                      devices=[
                                                                          BoundUbDevice(id=device.id, guid=device.guid,
                                                                                        type=device.type) for device in
                                                                          devices],
                                                                      ))
        logger.info("Bind UB devices finished, devices: {}, bus_guid: {}", device_list, bus_guid)


class UnbindNpuDeviceTask(AsyncTask):

    def __init__(self):
        self.npu_client = NpuClient()

    async def should_run(self) -> bool:
        # DeleteVmTask stores an empty value for a regular VM, so skip unbinding.
        if not self.context.get(UNBIND_REQUEST_CONTEXT_KEY):
            logger.info("Empty bus_guid, no UB devices to unbind.")
            return False
        return True

    async def execute(self) -> None:
        bus_guid: str = self.context.get(UNBIND_REQUEST_CONTEXT_KEY)
        inventory = await self.npu_client.get_host_ub_devices()
        device_list = [
            {"device_id": device.id, "device_type": device.type}
            for device in inventory
            if device.bus_guid == bus_guid
        ]
        if not device_list:
            raise UnbindDeviceNotFound(
                f"no UB devices bound to bus_guid: {bus_guid}"
            )
        logger.debug("Resolved unbind devices: {}, bus_guid: {}", device_list, bus_guid)
        try:
            await self.npu_client.free_devices(bus_guid, device_list)
        except Exception as exc:
            raise UnbindDeviceFailed from exc
        logger.info("Unbind UB devices finished, devices: {}, bus_guid: {}", device_list, bus_guid)
