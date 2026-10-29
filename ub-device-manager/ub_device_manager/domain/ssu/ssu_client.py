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
from typing import List, Optional

from ub_device_manager.app.models import (
    BindSsuVfeReq,
    NsConnectInfo,
    SsuAllocSpaceReq,
    SsuInfo,
    SsuNamespaceInfo,
    SsuNsStatus,
    SsuPermReq,
    SsuSpaceReq,
    UnbindSsuVfeReq,
    VfeForSsu,
)
from ub_device_manager.common.wrapper import try_catch_log
from ub_device_manager.constants import GB_TO_B
from ub_device_manager.exceptions import SsuNotFound


class SsuClient:
    """SSU domain client.

    All ``ubse.ubs_engine_ssu`` interfaces are imported lazily inside each
    method so importing this module does not require the native UBSE binding.
    """

    def __init__(self):
        pass

    @staticmethod
    def _vfe_to_ubse_vfe(vfe: VfeForSsu):
        """Convert the HTTP model ``VfeForSsu`` to the UBSE ``UbsUbVfe`` model."""
        from ubse.models.ubs_engine_model_ssu import UbsUbVfe

        return UbsUbVfe(
            slot_id=vfe.slot_id,
            chip_id=vfe.chip_id,
            die_id=vfe.die_id,
            pfe_id=vfe.pfe_id,
            vfe_id=vfe.vfe_id,
            vfe_guid=vfe.vfe_guid,
            bind_bus_instance_guid=vfe.bind_bus_guid or "",
        )

    @staticmethod
    def _ubse_vfe_to_vfe_for_ssu(vfe) -> VfeForSsu:
        """Convert the UBSE ``UbsUbVfe`` model to the HTTP model ``VfeForSsu``."""
        return VfeForSsu(
            slot_id=vfe.slot_id,
            chip_id=vfe.chip_id,
            die_id=vfe.die_id,
            pfe_id=vfe.pfe_id,
            vfe_id=vfe.vfe_id,
            vfe_guid=vfe.vfe_guid,
            bind_bus_guid=vfe.bind_bus_instance_guid,
        )

    @staticmethod
    def _ubse_alloc_result_to_ssu_info(result) -> SsuInfo:
        """Convert the UBSE ``UbsSsuAllocResult`` model to the HTTP model ``SsuInfo``."""
        return SsuInfo(
            name=result.name,
            strategy=int(result.strategy),
            namespace_cnt=len(result.namespaces),
            namespaces=[
                SsuNamespaceInfo(
                    tgt_eid=ns.tgt_eid,
                    tgt_nqn=ns.tgt_nqn,
                    ns_uuid=ns.ns_uuid,
                    ns_id=ns.namespace_id,
                    ns_dev_path=ns.ns_dev_path,
                    ns_size=ns.ns_size,
                    lba_format=int(ns.lba_format),
                )
                for ns in result.namespaces
            ],
        )

    @staticmethod
    def _tenant_to_bytes(tenant: Optional[str]) -> bytes:
        """Convert the JSON tenant string to the bytes accepted by the UBSE SDK."""
        return (tenant or "").encode("utf-8")

    @try_catch_log
    async def get_ns_connnect_info(self, name: str, vfe_guid: Optional[str] = None) -> List[NsConnectInfo]:
        """
        Query NVMe connection information for a storage space on a specified VFE.
        """
        from ubse.ubs_engine_ssu import ubs_ssu_connect_info_get

        target_vfe = None
        if vfe_guid:
            ssu_vfe_list = await self.get_ssu_vfe_list()
            vfe_for_ssu = next((vfe for vfe in ssu_vfe_list if vfe.vfe_guid == vfe_guid), None)
            if vfe_for_ssu is None:
                raise SsuNotFound(f"vfe_guid not found: {vfe_guid}")
            target_vfe = self._vfe_to_ubse_vfe(vfe_for_ssu)

        connect_info_list = await asyncio.to_thread(
            ubs_ssu_connect_info_get,
            name,
            target_vfe,
        )
        return [
            NsConnectInfo.model_validate(connect_info, from_attributes=True)
            for connect_info in connect_info_list
        ]

    @try_catch_log
    async def show_ssu_ns_status(self, name: str) -> List[SsuNsStatus]:
        """
        Get namespace usage information for a storage space.
        """
        from ubse.ubs_engine_ssu import ubs_ssu_ns_stats_get

        stats_list = await asyncio.to_thread(ubs_ssu_ns_stats_get, name)
        return [
            SsuNsStatus.model_validate(stats, from_attributes=True)
            for stats in stats_list
        ]

    @try_catch_log
    async def bind_ssu_vfe_bus(self, body: BindSsuVfeReq) -> str:
        """
        Bind a specified VFE to a bus.
        """
        from ubse.ubs_engine_ssu import ubs_ssu_fe_device_alloc

        ssu_vfe_list = await self.get_ssu_vfe_list()
        vfe_for_ssu = next((vfe for vfe in ssu_vfe_list if vfe.vfe_guid == body.vfe_guid), None)
        if vfe_for_ssu is None:
            raise SsuNotFound(f"vfe_guid not found: {body.vfe_guid}")

        ubse_vfe = self._vfe_to_ubse_vfe(vfe_for_ssu)
        return await asyncio.to_thread(
            ubs_ssu_fe_device_alloc, body.upi, ubse_vfe, body.bus_guid or ""
        )

    @try_catch_log
    async def get_ssu_vfe_list(self) -> List[VfeForSsu]:
        """
        List all SSU-specific VFEs.
        """
        from ubse.ubs_engine_ssu import ubs_ssu_fe_device_list

        fe_list = await asyncio.to_thread(ubs_ssu_fe_device_list)
        vfe_list: List[VfeForSsu] = []
        for fe in fe_list:
            for vfe in fe.vfe_list or []:
                vfe_list.append(self._ubse_vfe_to_vfe_for_ssu(vfe))
        return vfe_list

    @try_catch_log
    async def ssu_alloc(self, body: SsuAllocSpaceReq) -> SsuInfo:
        """
        Allocate SSU storage space.
        """
        from ubse.ubs_engine_ssu import ubs_ssu_space_alloc
        from ubse.models.ubs_engine_model_ssu import (
            UbsSsuAllocSpaceReq,
            UbsSsuAllocStrategy,
            UbsSsuLbaFormat,
        )

        ubse_req = UbsSsuAllocSpaceReq(
            name=body.name or "",
            ns_size=body.ns_size_gb * GB_TO_B,
            ns_num=body.ns_num or 1,
            lba_format=UbsSsuLbaFormat(int(body.lba_format)),
            strategy=UbsSsuAllocStrategy(int(body.strategy)),
            tenant=self._tenant_to_bytes(body.tenant),
        )
        raw_result = await asyncio.to_thread(ubs_ssu_space_alloc, ubse_req)
        return self._ubse_alloc_result_to_ssu_info(raw_result)

    @try_catch_log
    async def ssu_free(self, ssu_name: str) -> None:
        """
        Free SSU storage space.
        """
        from ubse.ubs_engine_ssu import ubs_ssu_space_free

        await asyncio.to_thread(ubs_ssu_space_free, ssu_name)

    @try_catch_log
    async def get_ssu_list(self) -> List[SsuInfo]:
        """
        List all allocated storage spaces.
        """
        from ubse.ubs_engine_ssu import ubs_ssu_alloc_info_list

        raw_result = await asyncio.to_thread(ubs_ssu_alloc_info_list)
        return [self._ubse_alloc_result_to_ssu_info(result) for result in raw_result]

    @try_catch_log
    async def show_ssu_alloc_info(self, name: str) -> SsuInfo:
        """
        Get allocated storage space information by name.

        UBSE does not expose a single-space query interface, so this method
        queries all allocated spaces and filters by name.
        """
        from ubse.ubs_engine_ssu import ubs_ssu_alloc_info_list

        raw_result = await asyncio.to_thread(ubs_ssu_alloc_info_list)
        for result in raw_result:
            if result.name == name:
                return self._ubse_alloc_result_to_ssu_info(result)
        raise SsuNotFound(f"ssu not found: {name}")

    @try_catch_log
    async def add_ssu_access_perm(self, params: SsuPermReq) -> None:
        """
        Add SSU access permission.
        """
        from ubse.ubs_engine_ssu import ubs_ssu_access_permission_add

        await asyncio.to_thread(ubs_ssu_access_permission_add, params.name, params.nqn)

    @try_catch_log
    async def remove_ssu_access_perm(self, params: SsuPermReq) -> None:
        """
        Remove SSU access permission.
        """
        from ubse.ubs_engine_ssu import ubs_ssu_access_permission_remove

        await asyncio.to_thread(ubs_ssu_access_permission_remove, params.name, params.nqn)

    @try_catch_log
    async def unbind_ssu_vfe_bus(self, body: UnbindSsuVfeReq) -> None:
        """
        Unbind a specified VFE from a bus GUID.
        """
        from ubse.ubs_engine_ssu import ubs_ssu_fe_device_free

        ssu_vfe_list = await self.get_ssu_vfe_list()
        vfe_for_ssu = next((vfe for vfe in ssu_vfe_list if vfe.vfe_guid == body.vfe_guid), None)
        if vfe_for_ssu is None:
            raise SsuNotFound(f"vfe_guid not found: {body.vfe_guid}")

        ubse_vfe = self._vfe_to_ubse_vfe(vfe_for_ssu)
        await asyncio.to_thread(ubs_ssu_fe_device_free, body.upi, ubse_vfe)

    @try_catch_log
    async def ssu_space_attach(self, req: SsuSpaceReq) -> List[str]:
        """
        Attach an allocated storage space.
        """
        from ubse.ubs_engine_ssu import ubs_ssu_space_attach
        from ubse.models.ubs_engine_model_ssu import UbsSsuSpaceReq

        ubse_req = UbsSsuSpaceReq(name=req.name, nqn=req.nqn or "", src_eid=req.src_eid or "")
        dev_paths = await asyncio.to_thread(ubs_ssu_space_attach, ubse_req)
        return list(dev_paths)

    @try_catch_log
    async def ssu_space_detach(self, req: SsuSpaceReq) -> None:
        """
        Detach an allocated storage space.
        """
        from ubse.ubs_engine_ssu import ubs_ssu_space_detach
        from ubse.models.ubs_engine_model_ssu import UbsSsuSpaceReq

        ubse_req = UbsSsuSpaceReq(name=req.name, nqn=req.nqn or "", src_eid=req.src_eid or "")
        await asyncio.to_thread(ubs_ssu_space_detach, ubse_req)
