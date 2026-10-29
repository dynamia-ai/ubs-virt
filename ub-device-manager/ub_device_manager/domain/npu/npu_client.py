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
import asyncio
from typing import Any, List, Optional

from pydantic import BaseModel, Field, constr



from ub_device_manager.common.wrapper import try_catch_log


class AffinityDeviceInfo(BaseModel):
    device_id: str = Field(..., description='Affinity device id.', examples=['1-1'])
    device_type: str = Field(
        ...,
        description='Affinity device type.',
        examples=['NIC_VFE'],
    )


class UbDeviceInfo(BaseModel):
    """Host UB device inventory entry, including NPUs and passthrough NICs."""
    id: constr(max_length=64) = Field(
        ..., description='NPU device ID (fixed-length string)', examples=['0-1']
    )
    type: constr(max_length=32) = Field(
        ..., description='NPU device type (fixed-length string)', examples=['NPU']
    )
    guid: constr(max_length=128) = Field(
        ...,
        description='NPU device GUID (fixed-length string)',
        examples=['0xcc08-a000-0-2-000000-00000000000201ff'],
    )
    bus_guid: Optional[constr(max_length=128)] = Field(
        None,
        description='NPU device BUS_INSTANCE_GUID (fixed-length string)',
        examples=['0xcc08-a002-0-0-000000-00000038c8c1008c'],
    )
    affinity_devs: List[AffinityDeviceInfo] = Field(
        default_factory=list,
        description='Devices that have an affinity relationship with the current UB device.',
    )


class DeviceRef(BaseModel):
    device_id: constr(min_length=1, max_length=64) = Field(
        ..., description='UB device ID', examples=['1-1']
    )
    device_type: constr(min_length=1, max_length=64) = Field(
        ..., description='UB device type', examples=['NPU']
    )


class AllocDevicesParams(BaseModel):
    """Input for the downstream component's device allocation interface."""
    upi: constr(min_length=1, max_length=64) = Field(
        ..., description='User isolation identifier', examples=['0x000f']
    )
    bus_guid: str = Field('', description='Existing bus instance GUID; pass an empty string to create a new bus instance.')
    device_list: List[DeviceRef] = Field(..., min_length=1, description='List of UB devices to allocate.')


# Device types allowed for VM passthrough.
UBSE_PASSTHROUGH_DEVICE_TYPES = {"NPU", "NIC_PFE", "NIC_VFE"}


_ubse_log_registered = False


def _register_ubse_log_callback() -> None:
    # The UBSE log callback is process-wide and must be registered once.
    global _ubse_log_registered
    if _ubse_log_registered:
        return
    from ubse.ubs_engine_log import ubs_engine_log_callback_register
    ubs_engine_log_callback_register(lambda level, msg: None)
    _ubse_log_registered = True


class NpuClient:
    def __init__(self):
        _register_ubse_log_callback()

    @staticmethod
    @try_catch_log
    async def get_host_ub_devices() -> list[UbDeviceInfo]:
        """Query and filter the host UB device inventory by the passthrough allowlist."""
        # Propagate UBSE failures after the decorator logs them; the API returns HTTP 500.
        from ubse.ubs_engine_npu import get_host_ub_devices
        host_devices = await asyncio.to_thread(get_host_ub_devices)
        return [UbDeviceInfo(
            type=dev.device_type,
            id=dev.device_id,
            guid=dev.guid,
            bus_guid=dev.bus_instance,
            affinity_devs=dev.affinity_devs)
            for dev in host_devices if dev.device_type in UBSE_PASSTHROUGH_DEVICE_TYPES]

    @try_catch_log
    async def bind_devices(self, params: AllocDevicesParams) -> tuple[str, int, int, int]:
        """Allocate devices and retrieve mapping data, releasing devices if retrieval fails."""
        from ubse.ubs_engine_npu import alloc_devices, free_devices, query_uba_tid_size
        device_list = [device.model_dump() for device in params.device_list]
        # UBSE accepts UPI values without the 0x prefix.
        ubse_upi = params.upi[2:] if params.upi.lower().startswith("0x") else params.upi
        bus_guid, _ = await asyncio.to_thread(
            alloc_devices,
            upi=ubse_upi,
            bus_guid=params.bus_guid,
            device_list=device_list,
        )
        try:
            tid, uba, size = await asyncio.to_thread(
                query_uba_tid_size,
                bus_instance_guid=bus_guid,
            )
            return bus_guid, tid, uba, size
        except Exception:
            # Release allocated devices when mapping retrieval fails.
            await asyncio.to_thread(
                free_devices,
                bus_instance_guid=bus_guid,
                device_list=device_list,
            )
            raise

    @staticmethod
    @try_catch_log
    async def free_devices(bus_instance_guid: str, device_list: list[dict[str, Any]]) -> None:
        from ubse.ubs_engine_npu import free_devices
        await asyncio.to_thread(
            free_devices,
            bus_instance_guid=bus_instance_guid,
            device_list=device_list,
        )

