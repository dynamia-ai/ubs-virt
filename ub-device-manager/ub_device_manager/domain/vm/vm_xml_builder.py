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
from typing import Any
from xml.etree import ElementTree as ET

from defusedxml import ElementTree as DefusedET

from ub_device_manager.app.models import BindNpuDeviceResult
from ub_device_manager.domain.vm.vm_smbios import SmbiosType131


QEMU_NS = "http://libvirt.org/schemas/domain/qemu/1.0"
DEFAULT_IOMMUFD_ID = "1"
UB_CONTROLLER_EID = "0x1"
UB_HOSTDEV_EID_OFFSET = 2
GUID_CONTROLLER_TYPE_VALUE = "2"
GUID_CONTROLLER_TYPE_INDEX = 3
MIN_GUID_PARTS_FOR_CONTROLLER_TYPE = 4


class VmXmlBuilder:
    def validate_input_xml(self, xml: str) -> None:
        root = DefusedET.fromstring(xml)
        self._validate_input_xml(root)

    def build(
        self,
        xml: str,
        bind_result: BindNpuDeviceResult,
        smbios_type131: SmbiosType131 | None = None,
    ) -> str:
        bus_guid = bind_result.bus_guid
        devices = [device.model_dump() for device in bind_result.devices]
        tid = bind_result.tid
        uba = bind_result.uba
        size = bind_result.size

        if not devices:
            raise ValueError("bind result devices is empty")
        if not bus_guid:
            raise ValueError("bus_guid is required to build UB VM XML")

        root = self._ensure_qemu_namespace(DefusedET.fromstring(xml))
        devices_node = self._get_or_create_devices(root)

        iommufd_id = self._ensure_iommufds(root)
        self._add_ub_controller(devices_node, bus_guid, len(devices))
        for index, device in enumerate(devices):
            self._add_ub_hostdev(devices_node, device, index, iommufd_id)
        self._add_iommu(devices_node)
        self._add_qemu_commandline(root, tid, uba, size)
        if smbios_type131 is not None:
            self._add_smbios_type131(root, smbios_type131)

        ET.indent(root, space="  ")
        return ET.tostring(root, encoding="unicode")

    def _add_smbios_type131(self, root: ET.Element, info: SmbiosType131) -> None:
        os_node = self._find_direct_child(root, "os")
        if os_node is None:
            raise ValueError("VM XML does not contain os element for SMBIOS configuration")
        smbios = self._find_direct_child(os_node, "smbios")
        if smbios is None:
            smbios = ET.SubElement(os_node, "smbios")
        smbios.set("mode", "sysinfo")

        sysinfo = next((node for node in self._find_direct_children(root, "sysinfo")
                        if node.get("type") == "smbios"), None)
        if sysinfo is None:
            sysinfo = ET.Element("sysinfo", type="smbios")
            root.insert(list(root).index(os_node), sysinfo)
        for old in self._find_direct_children(sysinfo, "type131"):
            sysinfo.remove(old)
        type131 = ET.SubElement(sysinfo, "type131")
        ET.SubElement(type131, "entry", **{
            "flag": str(info.flag),
            "pod_id": str(info.pod_id),
            "slot_id": str(info.slot_id),
            "mesh_type": str(info.mesh_type),
            "super_pod_id": str(info.super_pod_id),
            "served_index": str(info.served_index),
            "type_name": info.type_name,
        })

    def _validate_input_xml(self, root: ET.Element) -> None:
        if self._local_name(root.tag) != "domain":
            raise ValueError("VM XML root must be domain")
        if self._has_element(root, "controller", "type", "ub"):
            raise ValueError("VM XML already contains UB controller")
        if self._has_element(root, "hostdev", "type", "ub"):
            raise ValueError("VM XML already contains UB hostdev")
        if self._has_element(root, "arg", "value", "ubmem_vmmu", contains=True):
            raise ValueError("VM XML already contains ubmem_vmmu qemu argument")
        for iommufds in self._find_direct_children(root, "iommufds"):
            value = (iommufds.text or "").strip()
            if value and value != DEFAULT_IOMMUFD_ID:
                raise ValueError(f"unsupported iommufds value: {value}, expected {DEFAULT_IOMMUFD_ID}")

    def _ensure_qemu_namespace(self, root: ET.Element) -> ET.Element:
        ET.register_namespace("qemu", QEMU_NS)
        return root

    def _get_or_create_devices(self, root: ET.Element) -> ET.Element:
        devices_node = self._find_direct_child(root, "devices")
        if devices_node is None:
            devices_node = ET.SubElement(root, "devices")
        return devices_node

    def _ensure_iommufds(self, root: ET.Element) -> str:
        iommufds = self._find_direct_child(root, "iommufds")
        if iommufds is not None:
            value = (iommufds.text or "").strip()
            if not value:
                iommufds.text = DEFAULT_IOMMUFD_ID
                return DEFAULT_IOMMUFD_ID
            if value != DEFAULT_IOMMUFD_ID:
                raise ValueError(f"unsupported iommufds value: {value}, expected {DEFAULT_IOMMUFD_ID}")
            return value

        iommufds = ET.Element("iommufds")
        # UB hostdevs below reference this iommufd id through their iommufd attribute.
        iommufds.text = DEFAULT_IOMMUFD_ID
        devices_node = self._find_direct_child(root, "devices")
        insert_index = list(root).index(devices_node) if devices_node is not None else len(root)
        root.insert(insert_index, iommufds)
        return DEFAULT_IOMMUFD_ID

    def _add_ub_controller(
        self,
        devices_node: ET.Element,
        bus_instance_guid: str,
        device_count: int,
    ) -> None:
        controller = ET.SubElement(devices_node, "controller", type="ub", index="0")
        ET.SubElement(controller, "model", name="ubc")
        ET.SubElement(controller, "ports", num=str(device_count))
        ET.SubElement(controller, "alias", name="ubc.0")

        source = ET.SubElement(controller, "source")
        ET.SubElement(source, "businstance", guid=bus_instance_guid)
        ET.SubElement(
            controller,
            "address",
            type="ub",
            eid=UB_CONTROLLER_EID,
            guid=self._build_address_guid_from_bus(bus_instance_guid),
        )

    def _add_ub_hostdev(
        self,
        devices_node: ET.Element,
        device: dict[str, Any],
        index: int,
        iommufd_id: str,
    ) -> None:
        device_guid = device.get("guid")
        if not device_guid:
            raise ValueError(f"UB device guid is required to build VM XML, device: {device}")

        hostdev = ET.SubElement(
            devices_node,
            "hostdev",
            mode="subsystem",
            type="ub",
            managed="yes",
            iommufd=iommufd_id,
        )
        ET.SubElement(hostdev, "driver", name="vfio")
        source = ET.SubElement(hostdev, "source")
        ET.SubElement(source, "address", guid=device_guid)
        ET.SubElement(
            hostdev,
            "address",
            type="ub",
            eid=hex(index + UB_HOSTDEV_EID_OFFSET),
            guid=device_guid,
        )
        ET.SubElement(hostdev, "alias", name=f"ub-{index}")
        ports = ET.SubElement(hostdev, "ports", num="1")
        ET.SubElement(ports, "port", index="0", teid="0x1", tport=str(index))

    def _add_iommu(self, devices_node: ET.Element) -> None:
        for iommu in self._find_direct_children(devices_node, "iommu"):
            if iommu.get("model") == "ummu":
                return
        ET.SubElement(devices_node, "iommu", model="ummu")

    def _add_qemu_commandline(
        self,
        root: ET.Element,
        tid: int | str,
        uba: int | str,
        size: int | str,
    ) -> None:
        qemu_commandline = ET.Element(f"{{{QEMU_NS}}}commandline")
        ET.SubElement(qemu_commandline, f"{{{QEMU_NS}}}arg", value="-device")
        ET.SubElement(
            qemu_commandline,
            f"{{{QEMU_NS}}}arg",
            value=f"ubmem_vmmu,tid={tid},uba={uba},size={size}",
        )
        root.insert(0, qemu_commandline)

    def _has_element(
        self,
        root: ET.Element,
        local_name: str,
        attr_name: str,
        attr_value: str,
        contains: bool = False,
    ) -> bool:
        for element in root.iter():
            if self._local_name(element.tag) != local_name:
                continue
            value = element.get(attr_name, "")
            if contains and attr_value in value:
                return True
            if not contains and value == attr_value:
                return True
        return False

    def _find_direct_child(self, root: ET.Element, local_name: str) -> ET.Element | None:
        for child in list(root):
            if self._local_name(child.tag) == local_name:
                return child
        return None

    def _find_direct_children(self, root: ET.Element, local_name: str) -> list[ET.Element]:
        return [child for child in list(root) if self._local_name(child.tag) == local_name]

    def _local_name(self, tag: str) -> str:
        return tag.rsplit("}", 1)[-1] if "}" in tag else tag

    def _build_address_guid_from_bus(self, bus_instance_guid: str) -> str:
        parts = bus_instance_guid.split("-")
        if len(parts) < MIN_GUID_PARTS_FOR_CONTROLLER_TYPE:
            return bus_instance_guid
        parts[GUID_CONTROLLER_TYPE_INDEX] = GUID_CONTROLLER_TYPE_VALUE
        return "-".join(parts)
