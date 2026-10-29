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
import os
import asyncio
from typing import List

from defusedxml.ElementTree import ParseError
from loguru import logger
from pydantic import BaseModel

from ub_device_manager.app.models import BindNpuDeviceRequest, CreateVmRequest
from ub_device_manager.common.async_task_framework import AsyncTask
from ub_device_manager.common.config import CONFIG, parse_size
from ub_device_manager.exceptions import InvalidCreateVmRequest, VmXmlBuildFailed, VmXmlTooLarge
from ub_device_manager.domain.npu.npu_client import NpuClient
from ub_device_manager.constants import (
    BIND_REQUEST_CONTEXT_KEY,
    BIND_RESULT_CONTEXT_KEY,
    CREATE_VM_REQUEST_CONTEXT_KEY,
    DELETE_VM_CONTEXT_KEY,
    UNBIND_REQUEST_CONTEXT_KEY,
    VM_XML_CONTEXT_KEY,
)
from ub_device_manager.domain.vm.vm_client import VMClient, VmInfo
from ub_device_manager.domain.vm.vm_xml_builder import VmXmlBuilder
from ub_device_manager.domain.vm.vm_smbios import VmSmbios


class GetVmsListResp(BaseModel):
    vms: List[VmInfo]


class GetAllVmTask(AsyncTask):
    def __init__(self):
        self.vm_client = VMClient()

    async def execute(self) -> GetVmsListResp:
        logger.info(f"Get all vms start.")
        conn = None
        try:
            conn = await self.vm_client.connect()
            vms = await self.vm_client.get_all_vms(conn=conn)
            return GetVmsListResp(vms=vms)
        finally:
            await self.vm_client.disconnect(conn=conn)


class PrepareVmXmlTask(AsyncTask):
    """Validate the request and load the VM XML into the task context."""

    def __init__(self):
        self.xml_builder = VmXmlBuilder()

    async def should_run(self) -> bool:
        request: CreateVmRequest = self.context.get(CREATE_VM_REQUEST_CONTEXT_KEY)
        if not request:
            raise InvalidCreateVmRequest("create vm request not found in task context")
        self._validate_create_request(request)
        return True

    async def execute(self) -> None:
        request: CreateVmRequest = self.context.get(CREATE_VM_REQUEST_CONTEXT_KEY)
        xml = self._load_and_validate_xml(request, self.xml_builder)
        self.context.set(VM_XML_CONTEXT_KEY, xml)
        if request.npu_ids or (request.npu_count or 0) > 0:
            self.context.set(BIND_REQUEST_CONTEXT_KEY, BindNpuDeviceRequest(
                upi=request.upi,
                ids=request.npu_ids,
                count=request.npu_count,
                need_nic=request.need_nic,
            ))

    @staticmethod
    def _validate_create_request(request: CreateVmRequest) -> None:
        if request.npu_ids and (request.npu_count or 0) > 0:
            raise InvalidCreateVmRequest("npu_ids and npu_count are mutually exclusive")
        if request.npu_ids and len(set(request.npu_ids)) != len(request.npu_ids):
            raise InvalidCreateVmRequest("npu_ids contains duplicated id")
        if bool(request.xml_text) == bool(request.xml_path):
            raise InvalidCreateVmRequest("xml_text and xml_path must provide exactly one")
        if request.xml_text is not None and not request.xml_text.strip():
            raise InvalidCreateVmRequest("xml_text is empty")
        if request.xml_path is not None and not request.xml_path.strip():
            raise InvalidCreateVmRequest("xml_path is empty")

    @staticmethod
    def _load_and_validate_xml(request: CreateVmRequest, xml_builder: VmXmlBuilder) -> str:
        max_xml_size = parse_size(CONFIG.get("vm", {}).get("max_xml_size", "1 MB"))

        if request.xml_path is not None:
            xml_input = request.xml_path
            if not os.path.isfile(xml_input):
                logger.error("Invalid XML file path: {}", xml_input)
                raise InvalidCreateVmRequest("invalid xml input")
            try:
                file_size = os.path.getsize(xml_input)
            except OSError as exc:
                logger.error("Stat XML file failed: {}", exc)
                raise InvalidCreateVmRequest("invalid xml input") from exc
            if file_size > max_xml_size:
                logger.error(
                    "XML file too large: {} bytes, max allowed: {} bytes",
                    file_size,
                    max_xml_size,
                )
                raise VmXmlTooLarge(
                    f"xml file size {file_size} exceeds the maximum allowed size {max_xml_size}"
                )
            try:
                with open(xml_input, "r", encoding="utf-8") as xml_file:
                    xml_input = xml_file.read()
            except (OSError, UnicodeDecodeError) as exc:
                logger.error("Read XML file failed: {}", exc)
                raise InvalidCreateVmRequest("invalid xml input") from exc
        else:
            xml_input = request.xml_text
            text_size = len(xml_input.encode("utf-8"))
            if text_size > max_xml_size:
                logger.error(
                    "XML text too large: {} bytes, max allowed: {} bytes",
                    text_size,
                    max_xml_size,
                )
                raise VmXmlTooLarge(
                    f"xml content size {text_size} exceeds the maximum allowed size {max_xml_size}"
                )

        try:
            xml_builder.validate_input_xml(xml_input)
        except (ParseError, ValueError) as exc:
            logger.error("Invalid XML input: {}", exc)
            raise InvalidCreateVmRequest("invalid xml input") from exc

        return xml_input


class BuildAndStartVmTask(AsyncTask):
    """Build and start the VM; release allocated UB devices if this step fails."""

    def __init__(self):
        self.npu_client = NpuClient()
        self.vm_client = VMClient()
        self.xml_builder = VmXmlBuilder()
        self.vm_smbios = VmSmbios()

    async def execute(self) -> None:
        xml = self.context.get(VM_XML_CONTEXT_KEY)
        if not xml:
            raise VmXmlBuildFailed("loaded VM XML not found in task context")
        bind_result = self.context.get(BIND_RESULT_CONTEXT_KEY)
        if not bind_result:
            logger.info("Create VM without UB devices")
            await self.vm_client.define_and_start(xml)
            logger.info("Create VM finished without UB devices")
            return
        device_list = [
            {"device_id": device.id, "device_type": device.type}
            for device in bind_result.devices
        ]
        try:
            smbios_type131 = await asyncio.to_thread(self.vm_smbios.get_type131)
            final_xml = self.xml_builder.build(xml, bind_result, smbios_type131=smbios_type131)
            await self.vm_client.define_and_start(final_xml)
        except Exception:
            # Preserve the original failure if rollback also fails.
            try:
                await self.npu_client.free_devices(bind_result.bus_guid, device_list)
            except Exception:
                logger.error("Rollback allocated UB devices failed, bus_guid: {}", bind_result.bus_guid)
            raise

        logger.info(
            "Create VM finished, devices: {}, bus_guid: {}",
            device_list,
            bind_result.bus_guid,
        )


class DeleteVmTask(AsyncTask):
    def __init__(self):
        self.vm_client = VMClient()

    async def execute(self) -> None:
        vm_name = self.context.get(DELETE_VM_CONTEXT_KEY)
        logger.info(f"Deleting vm {vm_name} start.")
        conn = None
        try:
            conn = await self.vm_client.connect()
            vm = await self.vm_client.get_vm(conn=conn, name=vm_name)
            bus_instance_guid = ''
            if len(vm.controllers) > 0:
                bus_instance_guid = vm.controllers[0].source.get("businstance_guid", '')
            logger.info(f"The vm {vm_name}: state={vm.state}, bus_instance_guid={bus_instance_guid}.")

            await self.vm_client.delete_vm(conn=conn, name=vm_name)
            self.context.set(UNBIND_REQUEST_CONTEXT_KEY, bus_instance_guid)
        finally:
            await self.vm_client.disconnect(conn=conn)

