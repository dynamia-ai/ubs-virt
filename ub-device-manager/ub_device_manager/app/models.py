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

from enum import IntEnum
from typing import List, Optional

from pydantic import BaseModel, Field, conint, constr


class NpuCount(IntEnum):
    integer_1 = 1
    integer_2 = 2
    integer_4 = 4
    integer_8 = 8


class CreateVmRequest(BaseModel):
    xml_text: Optional[str] = Field(
        None,
        description='libvirt domain XML content. Exactly one of xml_text and xml_path must be provided.',
        examples=['<domain><name>vm</name></domain>'],
    )
    xml_path: Optional[str] = Field(
        None,
        description='Path to a libvirt domain XML file readable by the server. Exactly one of xml_text and xml_path must be provided.',
        examples=['/path/to/domain.xml'],
    )
    upi: constr(
        pattern=r'^0[xX]([1-9a-fA-F][0-9a-fA-F]{0,2}|0[1-9a-fA-F][0-9a-fA-F]{0,2}|00[1-9a-fA-F][0-9a-fA-F]?|000[1-9a-fA-F])$'
    ) = Field(
        ...,
        description='User isolation identifier in hexadecimal (one to four digits; prefix and digits are case-insensitive), from 0x0001 to 0x0fff.',
        examples=['0x000f'],
    )
    need_nic: Optional[bool] = Field(
        False,
        description='Whether to automatically select NIC devices based on NPU affinity.',
        examples=[False],
    )
    npu_ids: Optional[List[str]] = Field(
        None,
        description='Explicit NPU device IDs. Duplicates are not allowed and this field is mutually exclusive with npu_count.',
        examples=[['1-1', '1-2']],
    )
    npu_count: Optional[NpuCount] = Field(
        None,
        description='Number of free NPUs to select automatically; mutually exclusive with npu_ids. Only 1, 2, 4, or 8 are allowed. Omit both npu_ids and npu_count to create a regular VM without NPUs.',
        examples=[2],
    )


class Count(IntEnum):
    integer_1 = 1
    integer_2 = 2
    integer_4 = 4
    integer_8 = 8


class BindNpuDeviceRequest(BaseModel):
    upi: constr(
        pattern=r'^0[xX]([1-9a-fA-F][0-9a-fA-F]{0,2}|0[1-9a-fA-F][0-9a-fA-F]{0,2}|00[1-9a-fA-F][0-9a-fA-F]?|000[1-9a-fA-F])$'
    ) = Field(
        ...,
        description='User isolation identifier in hexadecimal (one to four digits; prefix and digits are case-insensitive), from 0x0001 to 0x0fff.',
        examples=['0x000f'],
    )
    ids: Optional[List[str]] = Field(
        None,
        description='Explicit NPU device IDs. Duplicates are not allowed; only 1, 2, 4, or 8 IDs are allowed and this field is mutually exclusive with count.',
        examples=[['1-1', '1-2']],
    )
    count: Optional[Count] = Field(
        None,
        description='Number of free NPUs to select automatically. Only 1, 2, 4, or 8 are allowed and this field is mutually exclusive with ids.',
        examples=[2],
    )
    need_nic: Optional[bool] = Field(
        False,
        description='Whether to automatically select NIC devices based on NPU affinity.',
        examples=[False],
    )


class UnbindNpuDeviceRequest(BaseModel):
    bus_guid: constr(max_length=128) = Field(
        ...,
        description='Bus instance GUID returned when UB devices were bound.',
        examples=['0xcc08-a002-0-0-000000-00000038c8c1008c'],
    )


class NsConnectInfo(BaseModel):
    src_eid: Optional[str] = Field('', description='Source EID.')
    tgt_eid: Optional[str] = Field('', description='Target EID.')
    tgt_nqn: Optional[str] = Field('', description='Target NQN address.')
    host_nqn: Optional[str] = Field('', description='Host NQN.')
    ns_uuid: Optional[str] = Field('', description='Namespace UUID.')
    ns_id: Optional[int] = Field(0, description='Namespace ID.')


class SsuNsStatus(BaseModel):
    ns_uuid: Optional[str] = Field('', description='Namespace UUID.')
    ns_id: Optional[int] = Field(0, description='Namespace ID.')
    total_size: Optional[int] = Field(0, description='Total namespace size.')
    used_size: Optional[int] = Field(0, description='Used namespace size.')


class BindSsuVfeReq(BaseModel):
    upi: int = Field(..., description='Tenant isolation identifier.')
    vfe_guid: str = Field(
        ..., description='SSU VFE GUID.', examples=['0000-0-0000-00000000']
    )
    bus_guid: constr(max_length=32) = Field(
        ...,
        description='Bus instance GUID. Can be empty (empty string means ubse creates a new VM bus instance internally); when non-empty, its length must be 32.',
        examples=[''],
    )


class UnbindSsuVfeReq(BaseModel):
    upi: int = Field(..., description='Tenant isolation identifier.')
    vfe_guid: str = Field(
        ..., description='SSU VFE GUID.', examples=['0000-0-0000-00000000']
    )


class VfeForSsu(BaseModel):
    slot_id: Optional[conint(ge=0, le=255)] = Field(0, description='Slot ID.')
    chip_id: Optional[conint(ge=0, le=255)] = Field(0, description='Chip ID.')
    die_id: Optional[conint(ge=0, le=255)] = Field(0, description='Die ID')
    pfe_id: Optional[conint(ge=0, le=65535)] = Field(
        0, description='Physical function element ID.'
    )
    vfe_id: Optional[conint(ge=0, le=65535)] = Field(
        0, description='Virtual function element ID.'
    )
    vfe_guid: Optional[str] = Field(None, description='vfe_guid')
    bind_bus_guid: Optional[str] = Field('', description='Bound bus instance GUID.')


class SsuVfeAllocReq(BaseModel):
    upi: int = Field(..., description='Tenant isolation identifier.')
    vfe_guid: str = Field(..., description='VFE GUID.')
    bus_guid: Optional[str] = Field(
        None, description='VM bus instance GUID, supplied as a hexadecimal string.'
    )


class SsuPermReq(BaseModel):
    name: str = Field(..., description='SSU name.', examples=['ssu-volume-001'])
    nqn: str = Field(
        ...,
        description='NVMe host NQN identifier.',
        examples=[
            'nqn.2014-08.org.nvmexpress:uuid:fedcba98-7654-3210-fedc-ba9876543210'
        ],
    )


class SsuLbaFormat(IntEnum):
    integer_512 = 512
    integer_4096 = 4096


class SsuAllocStrategy(IntEnum):
    integer_0 = 0
    integer_1 = 1
    integer_2 = 2


class SsuAllocSpaceReq(BaseModel):
    name: constr(min_length=1, max_length=48) = Field(
        ..., description='Request identifier, with a maximum length of 48 characters.'
    )
    ns_size_gb: conint(ge=1) = Field(
        ...,
        description='Requested total capacity in GB. Must be greater than or equal to 1.',
    )
    ns_num: conint(ge=1) = Field(
        ...,
        description='Number of namespaces. When ns_num is 1, strategy cannot be STRIPED (0).',
    )
    lba_format: SsuLbaFormat
    strategy: SsuAllocStrategy
    tenant: Optional[constr(pattern=r'^[a-zA-Z0-9_.:-]*$')] = Field(
        '',
        description='Requester tenant isolation identifier. Can be empty; when non-empty, only [a-zA-Z0-9_-.:] is allowed.',
    )


class SsuSpaceReq(BaseModel):
    name: constr(min_length=1, max_length=48) = Field(
        ..., description='SSU storage space identifier.', examples=['ssu-volume-001']
    )
    nqn: Optional[constr(max_length=69)] = Field(
        '',
        description='Host NVMe Qualified Name (NQN). Can be empty.',
        examples=[
            'nqn.2014-08.org.nvmexpress:uuid:fedcba98-7654-3210-fedc-ba9876543210'
        ],
    )
    src_eid: Optional[constr(max_length=17)] = Field(
        '',
        description='Source EID (Endpoint Identifier). Can be empty.',
        examples=['0x0000'],
    )


class SsuAttachResult(BaseModel):
    dev_paths: List[constr(max_length=128)] = Field(
        ...,
        description='Device paths returned after the storage space is attached.',
        examples=[['/dev/nvme0n1']],
    )


class SsuNamespaceInfo(BaseModel):
    tgt_eid: constr(max_length=64) = Field(
        ...,
        description='Target EID (Endpoint Identifier).',
        examples=[
            'nqn.2014-08.org.nvmexpress:uuid:fedcba98-7654-3210-fedc-ba9876543210'
        ],
    )
    tgt_nqn: constr(max_length=256) = Field(
        ...,
        description='Target NQN (NVMe Qualified Name).',
        examples=['nqn.2014-08.com.example:nvme:subsystem-1'],
    )
    ns_uuid: constr(max_length=64) = Field(
        ...,
        description='Namespace UUID.',
        examples=['123e4567-e89b-12d3-a456-426614174000'],
    )
    ns_id: conint(ge=0) = Field(..., description='Namespace ID.', examples=[1])
    ns_dev_path: constr(max_length=128) = Field(
        ..., description='Namespace device path in the VM.', examples=['/dev/nvme0n1']
    )
    ns_size: conint(ge=0) = Field(
        ..., description='Namespace size in bytes.', examples=[1099511627776]
    )
    lba_format: int = Field(
        ..., description='LBA format (Logical Block Address Format).', examples=[0]
    )


class Message(BaseModel):
    msg: constr(max_length=256) = Field(
        ..., description='Returned message.', examples=['success.']
    )


class BoundUbDevice(BaseModel):
    id: constr(max_length=64) = Field(
        ..., description='UB device ID.', examples=['1-1']
    )
    guid: constr(max_length=128) = Field(
        ...,
        description='UB device GUID.',
        examples=['0xcc08-a000-0-2-000000-00000000000201ff'],
    )
    type: constr(max_length=64) = Field(
        ..., description='UB device type, such as NPU or NIC_VFE.', examples=['NPU']
    )


class NpuDeviceInfo(BaseModel):
    id: constr(max_length=64) = Field(
        ..., description='NPU device ID (fixed-length string).', examples=['0-1']
    )
    guid: constr(max_length=128) = Field(
        ...,
        description='NPU device GUID (fixed-length string).',
        examples=['0xcc08-a000-0-2-000000-00000000000201ff'],
    )
    bus_guid: Optional[constr(max_length=128)] = Field(
        None,
        description='Bus instance GUID bound to the NPU device (fixed-length string).',
        examples=['0xcc08-a002-0-0-000000-00000038c8c1008c'],
    )


class BindNpuDeviceResult(BaseModel):
    tid: int = Field(..., description='Bus instance TID.', examples=[11])
    uba: int = Field(..., description='Bus instance UBA.', examples=[22])
    size: int = Field(..., description='Bus instance memory size.', examples=[1024])
    bus_guid: constr(max_length=128) = Field(
        ...,
        description='Bus instance GUID returned after device allocation.',
        examples=['0xcc08-a002-0-0-000000-00000038c8c1008c'],
    )
    devices: List[BoundUbDevice] = Field(..., description='Bound UB devices.')


class SsuInfo(BaseModel):
    name: constr(max_length=256) = Field(
        ..., description='SSU name (fixed-length string).', examples=['ssu-volume-001']
    )
    strategy: int = Field(..., description='Allocation strategy.', examples=[1])
    namespace_cnt: conint(ge=0) = Field(
        ..., description='Number of namespaces.', examples=[2]
    )
    namespaces: List[SsuNamespaceInfo] = Field(
        ..., description='Namespace information entries.'
    )
