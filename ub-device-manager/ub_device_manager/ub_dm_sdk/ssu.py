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
from typing import List, Optional

from .http_client import UbDeviceManagerClient
from .models import (
    SsuAllocSpaceReq,
    SsuAllocStrategy,
    SsuInfo,
    SsuLbaFormat,
)


def get_ssu_list(
    client: Optional[UbDeviceManagerClient] = None,
) -> List[SsuInfo]:
    """
    Query all allocated SSU storage spaces.

    Return:
        List[SsuInfo]: Allocated SSU storage space information.
    """
    sdk_client = client or UbDeviceManagerClient()
    response = sdk_client.request("GET", "/ssu")
    return [SsuInfo.model_validate(item) for item in response or []]


def get_ssu(
    name: str,
    client: Optional[UbDeviceManagerClient] = None,
) -> SsuInfo:
    """
    Query a single allocated SSU storage space by name.

    Args:
        name: SSU storage space identifier.

    Return:
        SsuInfo: Allocated SSU storage space information.
    """
    sdk_client = client or UbDeviceManagerClient()
    response = sdk_client.request("GET", f"/ssu/{name}")
    return SsuInfo.model_validate(response)


def alloc_ssu(
    name: str,
    ns_size_gb: int,
    ns_num: int,
    lba_format: SsuLbaFormat = SsuLbaFormat.FORMAT_512,
    strategy: SsuAllocStrategy = SsuAllocStrategy.NORMAL,
    tenant: Optional[str] = None,
    client: Optional[UbDeviceManagerClient] = None,
) -> SsuInfo:
    """
    Allocate SSU storage space.

    Args:
        name: SSU storage space identifier, maximum 48 characters.
        ns_size_gb: Requested total capacity in GB; must be greater than or equal to 1.
        ns_num: Number of namespaces; must be greater than 0.
        lba_format: LBA format (512 or 4096).
        strategy: Allocation strategy (STRIPED, LINEAR or NORMAL).
        tenant: Optional tenant isolation identifier.

    Return:
        SsuInfo: Allocation result, including namespace information.
    """
    request = SsuAllocSpaceReq(
        name=name,
        ns_size_gb=ns_size_gb,
        ns_num=ns_num,
        lba_format=lba_format,
        strategy=strategy,
        tenant=tenant,
    )
    sdk_client = client or UbDeviceManagerClient()
    response = sdk_client.post("/ssu/alloc", json=request.model_dump(exclude_none=True))
    return SsuInfo.model_validate(response)


def free_ssu(
    name: str,
    client: Optional[UbDeviceManagerClient] = None,
) -> None:
    """
    Free an allocated SSU storage space by name.

    Args:
        name: SSU storage space identifier.
    """
    sdk_client = client or UbDeviceManagerClient()
    sdk_client.request("DELETE", f"/ssu/{name}")
