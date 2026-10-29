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
from typing import Optional, Tuple

from .http_client import UbDeviceManagerClient
from .models import BindNpuDeviceRequest, BoundUbDevice, NpuDeviceInfo, UnbindNpuDeviceRequest


def get_host_npu_device(
    client: Optional[UbDeviceManagerClient] = None,
) -> list[NpuDeviceInfo]:
    """
    Query NPU device information for the current host.

    Return:
        list[NpuDeviceInfo]: List of NPU devices.
    """
    sdk_client = client or UbDeviceManagerClient()
    response = sdk_client.request("GET", "/npu")
    return [NpuDeviceInfo.model_validate(device) for device in response or []]


def bind_npu_device(
    upi: str,
    ids: Optional[list[str]] = None,
    count: Optional[int] = None,
    need_nic: bool = False,
    client: Optional[UbDeviceManagerClient] = None,
) -> Tuple[int, int, int, str, list[BoundUbDevice]]:
    """
    Bind NPU devices.

    Args:
        upi: User isolation identifier, from 0x0000 to 0x1000.
        ids: Explicit NPU device IDs, mutually exclusive with count; only 1, 2, 4, or 8 IDs are allowed.
        count: Number of free NPUs to select automatically, mutually exclusive with ids; only 1, 2, 4, or 8 are allowed.
        need_nic: Whether to automatically select affinity NIC devices for the NPUs.

    Return:
        Tuple[int, int, int, str, list[BoundUbDevice]]: tid, uba, size, bus_guid,
        Bound devices, each with id, guid, and type; includes affinity NICs when need_nic is true.
    """
    request = BindNpuDeviceRequest(upi=upi, ids=ids, count=count, need_nic=need_nic)
    sdk_client = client or UbDeviceManagerClient()
    response = sdk_client.post("/npu/bind", json=request.model_dump(exclude_none=True)) or {}
    devices = [BoundUbDevice.model_validate(device) for device in response.get("devices", [])]
    return (
        response.get("tid"),
        response.get("uba"),
        response.get("size"),
        response.get("bus_guid"),
        devices,
    )


def unbind_npu_device(
    bus_guid: str,
    client: Optional[UbDeviceManagerClient] = None,
) -> None:
    """
    Unbind NPU devices.

    Args:
        bus_guid: Bus instance GUID returned when NPU devices were bound.
    """
    request = UnbindNpuDeviceRequest(bus_guid=bus_guid)
    sdk_client = client or UbDeviceManagerClient()
    sdk_client.post("/npu/unbind", json=request.model_dump())
