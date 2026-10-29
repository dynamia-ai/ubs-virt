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
from typing import Any, Optional

from .http_client import UbDeviceManagerClient
from .models import CreateVmRequest


def create_vm(
    upi: str,
    xml_text: Optional[str] = None,
    xml_path: Optional[str] = None,
    need_nic: bool = False,
    npu_ids: Optional[list[str]] = None,
    npu_count: Optional[int] = None,
    client: Optional[UbDeviceManagerClient] = None,
) -> Any:
    """
    Create VM.

    Args:
        upi: User partition id, 0x0000 ~ 0x1000.
        xml_text: libvirt domain XML content, mutually exclusive with xml_path.
        xml_path: libvirt domain XML file path readable by server, mutually exclusive with xml_text.
        need_nic: Whether to select NIC devices by NPU affinity.
        npu_ids: Explicit NPU device id list, mutually exclusive with positive npu_count.
        npu_count: Number of NPU devices to select automatically. 0 means no NPU device is requested.
    """
    request = CreateVmRequest(
        xml_text=xml_text,
        xml_path=xml_path,
        upi=upi,
        need_nic=need_nic,
        npu_ids=npu_ids,
        npu_count=npu_count,
    )
    sdk_client = client or UbDeviceManagerClient()
    return sdk_client.post("/vm/create", json=request.model_dump(exclude_none=True))


def delete_vm(
    vm_name: str,
    client: Optional[UbDeviceManagerClient] = None,
) -> None:
    """
    Delete VM.

    Args:
        vm_name: VM name.
    """
    sdk_client = client or UbDeviceManagerClient()
    return sdk_client.request("DELETE", f"/vm/{vm_name}")
