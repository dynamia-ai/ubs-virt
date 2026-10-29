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
"""Unit tests for :mod:`ub_device_manager.domain.vm.vm_xml_builder`."""
from xml.etree import ElementTree as ET

import pytest
from defusedxml.ElementTree import ParseError

from ub_device_manager.app.models import BindNpuDeviceResult, BoundUbDevice
from ub_device_manager.domain.vm.vm_xml_builder import QEMU_NS, VmXmlBuilder

BASE_XML = """
<domain type='kvm'>
  <name>vm-1</name>
  <devices></devices>
</domain>
"""


def make_bind_result(guids=("guid-1",), bus_guid="0xcc08-a002-0-0-000000-00000038c8c1008c"):
    return BindNpuDeviceResult(
        tid=11,
        uba=22,
        size=1024,
        bus_guid=bus_guid,
        devices=[BoundUbDevice(id=f"1-{index}", guid=guid, type="NPU") for index, guid in enumerate(guids)],
    )


def parse(xml):
    return ET.fromstring(xml)


def find_all(root, tag):
    return root.findall(f".//{tag}")


class TestValidateInputXml:
    async def test_accepts_plain_domain_xml(self):
        VmXmlBuilder().validate_input_xml(BASE_XML)

    async def test_rejects_non_domain_root(self):
        with pytest.raises(ValueError, match="root must be domain"):
            VmXmlBuilder().validate_input_xml("<devices/>")

    async def test_rejects_existing_ub_controller(self):
        xml = "<domain><devices><controller type='ub'/></devices></domain>"

        with pytest.raises(ValueError, match="already contains UB controller"):
            VmXmlBuilder().validate_input_xml(xml)

    async def test_rejects_existing_ub_hostdev(self):
        xml = "<domain><devices><hostdev type='ub'/></devices></domain>"

        with pytest.raises(ValueError, match="already contains UB hostdev"):
            VmXmlBuilder().validate_input_xml(xml)

    async def test_rejects_existing_ubmem_vmmu_argument(self):
        xml = f"<domain xmlns:qemu='{QEMU_NS}'><qemu:commandline><qemu:arg value='ubmem_vmmu,tid=1'/></qemu:commandline></domain>"

        with pytest.raises(ValueError, match="already contains ubmem_vmmu"):
            VmXmlBuilder().validate_input_xml(xml)

    async def test_rejects_unsupported_iommufds_value(self):
        xml = "<domain><iommufds>2</iommufds></domain>"

        with pytest.raises(ValueError, match="unsupported iommufds value"):
            VmXmlBuilder().validate_input_xml(xml)

    async def test_accepts_supported_iommufds_value(self):
        VmXmlBuilder().validate_input_xml("<domain><iommufds>1</iommufds></domain>")

    async def test_invalid_xml_raises_parse_error(self):
        with pytest.raises(ParseError):
            VmXmlBuilder().validate_input_xml("<domain>")


class TestBuild:
    async def test_rejects_empty_devices(self):
        bind_result = make_bind_result()
        bind_result.devices = []

        with pytest.raises(ValueError, match="bind result devices is empty"):
            VmXmlBuilder().build(BASE_XML, bind_result)

    async def test_rejects_empty_bus_guid(self):
        bind_result = make_bind_result(bus_guid="")

        with pytest.raises(ValueError, match="bus_guid is required"):
            VmXmlBuilder().build(BASE_XML, bind_result)

    async def test_rejects_device_without_guid(self):
        class FakeDevice:
            def model_dump(self):
                return {"id": "1-1"}

        class FakeResult:
            bus_guid = "bus"
            tid, uba, size = 1, 2, 3
            devices = [FakeDevice()]

        with pytest.raises(ValueError, match="guid is required"):
            VmXmlBuilder().build(BASE_XML, FakeResult())

    async def test_builds_controller_hostdevs_iommu_and_commandline(self):
        result = VmXmlBuilder().build(BASE_XML, make_bind_result(guids=("g1", "g2")))
        root = parse(result)

        controllers = find_all(root, "controller")
        assert len(controllers) == 1
        assert controllers[0].get("type") == "ub"
        assert controllers[0].find("model").get("name") == "ubc"
        assert controllers[0].find("ports").get("num") == "2"
        assert controllers[0].find("source/businstance").get("guid") == "0xcc08-a002-0-0-000000-00000038c8c1008c"

        hostdevs = find_all(root, "hostdev")
        assert [hostdev.find("source/address").get("guid") for hostdev in hostdevs] == ["g1", "g2"]
        assert hostdevs[0].get("iommufd") == "1"
        assert hostdevs[0].find("address").get("eid") == hex(2)

        iommus = find_all(root, "iommu")
        assert len(iommus) == 1
        assert iommus[0].get("model") == "ummu"

    async def test_adds_qemu_commandline_as_first_child(self):
        result = VmXmlBuilder().build(BASE_XML, make_bind_result())
        root = parse(result)

        assert root[0].tag == f"{{{QEMU_NS}}}commandline"
        args = [child.get("value") for child in root[0]]
        assert args == ["-device", "ubmem_vmmu,tid=11,uba=22,size=1024"]

    async def test_creates_iommufds_before_devices(self):
        result = VmXmlBuilder().build(BASE_XML, make_bind_result())
        root = parse(result)

        iommufds = root.find("iommufds")
        assert iommufds is not None
        assert iommufds.text == "1"

    async def test_reuses_existing_iommufds_value(self):
        xml = "<domain><iommufds>1</iommufds><devices/></domain>"
        result = VmXmlBuilder().build(xml, make_bind_result())
        root = parse(result)

        assert len(root.findall("iommufds")) == 1

    async def test_fills_empty_iommufds_value(self):
        xml = "<domain><iommufds></iommufds><devices/></domain>"
        result = VmXmlBuilder().build(xml, make_bind_result())
        root = parse(result)

        assert root.find("iommufds").text == "1"

    async def test_build_rejects_unsupported_iommufds_value(self):
        xml = "<domain><iommufds>2</iommufds><devices/></domain>"

        with pytest.raises(ValueError, match="unsupported iommufds value"):
            VmXmlBuilder().build(xml, make_bind_result())

    async def test_existing_ummu_iommu_is_not_duplicated(self):
        xml = "<domain><devices><iommu model='ummu'/></devices></domain>"
        result = VmXmlBuilder().build(xml, make_bind_result())
        root = parse(result)

        assert len(root.findall(".//iommu")) == 1

    async def test_creates_devices_node_when_missing(self):
        xml = "<domain><name>vm</name></domain>"
        result = VmXmlBuilder().build(xml, make_bind_result())
        root = parse(result)

        assert root.find("devices") is not None

    async def test_address_guid_uses_controller_type_value(self):
        result = VmXmlBuilder().build(
            BASE_XML,
            make_bind_result(bus_guid="aaaa-bbbb-cccc-dddd"),
        )
        root = parse(result)

        controller_address = root.find(".//controller/address")
        assert controller_address.get("guid") == "aaaa-bbbb-cccc-2"
        assert controller_address.get("eid") == "0x1"

    async def test_short_bus_guid_is_kept_as_is(self):
        assert VmXmlBuilder()._build_address_guid_from_bus("short-guid") == "short-guid"

    async def test_local_name_strips_namespace(self):
        assert VmXmlBuilder()._local_name("{ns}domain") == "domain"
        assert VmXmlBuilder()._local_name("domain") == "domain"

class TestSmbiosType131:
    async def test_injects_type131_and_keeps_other_smbios(self):
        from ub_device_manager.domain.vm.vm_smbios import SmbiosType131
        original = """<domain><sysinfo type='smbios'><system><entry name='manufacturer'>Vendor</entry></system>
          <type131><entry flag='1'/></type131></sysinfo><os><type>hvm</type></os><devices/></domain>"""
        info = SmbiosType131(0, 65535, 255, 1, 16383, 3)
        xml = VmXmlBuilder().build(original, make_bind_result(), smbios_type131=info)
        root = parse(xml)
        assert root.find('os/smbios').get('mode') == 'sysinfo'
        assert root.find("sysinfo/system/entry").text == 'Vendor'
        entries = root.findall('sysinfo/type131/entry')
        assert len(entries) == 1
        assert entries[0].attrib == {
            'flag': '0', 'pod_id': '65535', 'slot_id': '255', 'mesh_type': '1',
            'super_pod_id': '16383', 'served_index': '3', 'type_name': 'UB INFO',
        }

    async def test_does_not_inject_when_info_absent(self):
        root = parse(VmXmlBuilder().build("<domain><os><type>hvm</type></os><devices/></domain>", make_bind_result()))
        assert root.find('sysinfo') is None
        assert root.find('os/smbios') is None
