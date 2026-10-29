##########################################################################################################
# Copyright (c) Huawei Technologies Co., Ltd. 2026. All rights reserved.
#
# ub-device-manager is licensed under the Mulan PSL v2.
# You can use this software according to the terms and conditions of the Mulan PSL v2.
# You may obtain a copy of Mulan PSL v2 at:
#     http://license.coscl.org.cn/MulanPSL2
# THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR
# IMPLIED, INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY OR FIT FOR A PARTICULAR PURPOSE.
# See the Mulan PSL v2 for more details.
##########################################################################################################
from enum import IntEnum
from typing import List, Optional

from pydantic import BaseModel, ConfigDict, Field


class CreateVmRequest(BaseModel):
    model_config = ConfigDict(extra="forbid")

    xml_text: Optional[str] = None
    xml_path: Optional[str] = None
    upi: str
    need_nic: bool = False
    npu_ids: Optional[list[str]] = None
    npu_count: Optional[int] = Field(None, ge=0)


class BindNpuDeviceRequest(BaseModel):
    upi: str
    ids: Optional[list[str]] = None
    count: Optional[int] = Field(None, ge=1)
    need_nic: bool = False


class UnbindNpuDeviceRequest(BaseModel):
    bus_guid: str


class BoundUbDevice(BaseModel):
    id: str
    guid: str
    type: str


class NpuDeviceInfo(BaseModel):
    id: str
    guid: str
    bus_guid: Optional[str] = None


class SsuLbaFormat(IntEnum):
    FORMAT_512 = 512
    FORMAT_4K = 4096


class SsuAllocStrategy(IntEnum):
    STRIPED = 0
    LINEAR = 1
    NORMAL = 2


class SsuAllocSpaceReq(BaseModel):
    model_config = ConfigDict(extra="forbid")

    name: str = Field(..., min_length=1, max_length=48)
    ns_size_gb: int = Field(..., ge=1)
    ns_num: int = Field(..., ge=1)
    lba_format: SsuLbaFormat = SsuLbaFormat.FORMAT_512
    strategy: SsuAllocStrategy = SsuAllocStrategy.NORMAL
    tenant: Optional[str] = None


class SsuNamespaceInfo(BaseModel):
    tgt_eid: str = ""
    tgt_nqn: str = ""
    ns_uuid: str = ""
    ns_id: int = 0
    ns_dev_path: str = ""
    ns_size: int = 0
    lba_format: int = 0


class SsuInfo(BaseModel):
    name: str
    strategy: int
    namespace_cnt: int
    namespaces: List[SsuNamespaceInfo] = Field(default_factory=list)
