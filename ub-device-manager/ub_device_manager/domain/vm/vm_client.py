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
import os
from typing import List, Dict
from xml.etree import ElementTree as ET

import libvirt
from pydantic import BaseModel, Field
from loguru import logger

from ub_device_manager.exceptions import (
    GetVmInfoFailed,
    LibvirtConnectionFailed,
    VmDefineStartFailed,
    VmDeletionFailed,
)
DEFAULT_LIBVIRT_URI = "qemu:///system"


class ControllerInfo(BaseModel):
    """Controller device information."""
    ctrl_type: str          # XML type attribute
    index: str              # XML index attribute
    model: str              # XML model attribute or child element
    source: Dict[str, str] = Field(default_factory=dict)


class VmInfo(BaseModel):
    """Virtual machine information."""
    name: str
    uuid: str
    state: str
    controllers: List[ControllerInfo] = Field(default_factory=list)


class VMClient:
    def __init__(self):
        self._uri = DEFAULT_LIBVIRT_URI

    async def connect(self) -> libvirt.virConnect:
        """Connect to libvirt."""
        try:
            return await asyncio.to_thread(libvirt.open, self._uri)
        except Exception as e:
            logger.error(f"Failed to connect to libvirt, error: {e}")
            raise LibvirtConnectionFailed from e

    async def get_vm(self, conn: libvirt.virConnect, name: str) -> VmInfo:
        """
        Get information for the VM with the specified name.

        :param conn: libvirt connection
        :param name: VM name
        """
        try:
            domain = await asyncio.to_thread(conn.lookupByName, name)
            name = await asyncio.to_thread(domain.name)
            uuid = await asyncio.to_thread(domain.UUIDString)
            state = await self._state_to_str((await asyncio.to_thread(domain.state))[0])
            xml_desc = await asyncio.to_thread(domain.XMLDesc)
            controllers = await self._parse_controllers(xml_desc=xml_desc)
            return VmInfo(name=name, uuid=uuid, state=state, controllers=controllers)
        except Exception as e:
            logger.error(f"Failed to get VM: {name}, error: {e}")
            raise GetVmInfoFailed from e

    async def get_all_vms(self, conn: libvirt.virConnect, vm_state = 0) -> List[VmInfo]:
        """
        Get information for all VMs.

        :param conn: libvirt connection
        :param vm_state: VM state flags
        """
        vms = []
        try:
            domains = await asyncio.to_thread(conn.listAllDomains, vm_state)
            for domain in domains:
                name = await asyncio.to_thread(domain.name)
                uuid = await asyncio.to_thread(domain.UUIDString)
                state = await self._state_to_str((await asyncio.to_thread(domain.state))[0])
                xml_desc = await asyncio.to_thread(domain.XMLDesc, 0)
                controllers = await self._parse_controllers(xml_desc=xml_desc)

                vms.append(VmInfo(
                    name=name,
                    uuid=uuid,
                    state=state,
                    controllers=controllers,
                ))
            return vms
        except Exception as e:
            logger.error("Failed to get all vms, error: %s", e)
            raise GetVmInfoFailed from e

    async def delete_vm(self, conn: libvirt.virConnect, name: str) -> str:
        """
        Delete a VM.

        :param conn: libvirt connection
        :param name: VM name
        """
        return await asyncio.to_thread(self._delete_vm, conn, name)

    def _delete_vm(self, conn: libvirt.virConnect, name: str) -> None:
        try:
            domain = conn.lookupByName(name)
            if domain.isActive():
                domain.destroy()   # Destroying an inactive domain raises an error.
            domain.undefineFlags(libvirt.VIR_DOMAIN_UNDEFINE_NVRAM)

        except Exception as e:
            logger.error("Failed to delete vm {}, error: {}.", name, e)
            raise VmDeletionFailed from e

    @staticmethod
    async def _parse_controllers(xml_desc: str) -> List[ControllerInfo]:
        """
        Parse <controller> nodes from the domain XML.

        :param xml_desc: VM XML text
        """
        guest = ET.fromstring(xml_desc)
        controllers = []

        for ctrl in guest.findall(".//devices/controller"):
            ctrl_type = ctrl.get("type", "unknown")
            # Ignore controllers that are not UB device configuration sections.
            if ctrl_type != "ub":
                continue

            index = ctrl.get("index", "")
            model = ctrl.get("model", "")

            source = {}
            source_node = ctrl.find("source")
            if source_node is not None:
                businstance = source_node.find("businstance")
                if businstance is not None:
                    source["businstance_guid"] = businstance.get("guid", "")

            controllers.append(ControllerInfo(
                ctrl_type=ctrl_type,
                index=index,
                model=model,
                source=source,
            ))

        return controllers

    @staticmethod
    async def _state_to_str(state: int) -> str:
        """
        Convert a state code to a string.

        :param state: VM state code
        """
        state_map = {
            libvirt.VIR_DOMAIN_NOSTATE: "no state",
            libvirt.VIR_DOMAIN_RUNNING: "running",
            libvirt.VIR_DOMAIN_BLOCKED: "blocked",
            libvirt.VIR_DOMAIN_PAUSED: "paused",
            libvirt.VIR_DOMAIN_SHUTDOWN: "shutdown",
            libvirt.VIR_DOMAIN_SHUTOFF: "shut off",
            libvirt.VIR_DOMAIN_CRASHED: "crashed",
            libvirt.VIR_DOMAIN_PMSUSPENDED: "suspended",
        }
        return state_map.get(state, f"unknown({state})")

    @staticmethod
    async def disconnect(conn: libvirt.virConnect) -> None:
        """
        Disconnect from libvirt.

        :param conn: libvirt connection
        """
        if conn is not None:
            try:
                await asyncio.to_thread(conn.close)
            except Exception as e:
                logger.error("Failed to disconnect to libvirt, error: %s", e)

    async def define_and_start(self, xml: str) -> None:
        """Define and start a VM; undefine it if startup fails."""
        conn = await self.connect()
        domain = None
        try:
            logger.info("Define VM domain by XML")
            domain = await asyncio.to_thread(conn.defineXML, xml)
            if domain is None:
                raise VmDefineStartFailed("Failed to define VM domain")

            domain_name = await asyncio.to_thread(domain.name)
            logger.info("Start VM domain: {}", domain_name)
            await asyncio.to_thread(domain.create)
            logger.info("VM domain started: {}", domain_name)
        except Exception as exc:
            # Undefine a successfully defined domain if startup fails.
            if domain is not None:
                await asyncio.to_thread(
                    domain.undefineFlags,
                    libvirt.VIR_DOMAIN_UNDEFINE_NVRAM,
                )
            if isinstance(exc, VmDefineStartFailed):
                raise
            logger.error("Define/start VM failed, error: {}", exc)
            raise VmDefineStartFailed("Failed to define/start VM domain") from exc
        finally:
            await self.disconnect(conn)
