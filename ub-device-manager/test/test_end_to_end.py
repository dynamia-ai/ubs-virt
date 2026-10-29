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
"""End-to-end tests across the whole stack.

These tests drive the real HTTP API (``create_app``), the real async task
chains, the real domain tasks and the real clients.  Only the native
boundaries are replaced:

* ``ubse.*``    - device/storage backend (mocked through ``unittest.mock.patch``)
* ``libvirt``   - hypervisor (mocked through ``unittest.mock.patch``)

Everything in between is production code, so these tests verify that the
layers are wired together correctly (task context keys, request/response
models, rollback behaviour, error-code mapping).
"""
from types import SimpleNamespace
from unittest.mock import patch

import pytest
from fastapi.testclient import TestClient

from ub_device_manager.common.factory import create_app
from ub_device_manager.domain.npu.npu_client import AffinityDeviceInfo

VM_XML = "<domain><os><type>hvm</type></os><devices/></domain>"

VM_XML_WITH_UB = """
<domain type='kvm'>
  <name>vm-1</name>
  <devices>
    <controller type='ub' index='0' model='ubc'>
      <source><businstance guid='bus-1'/></source>
    </controller>
  </devices>
</domain>
"""


# ---------------------------------------------------------------------------
# Native-boundary fakes
# ---------------------------------------------------------------------------
class FakeUbseDevice:
    """Mirror of the ubse host-device object returned by get_host_ub_devices."""

    def __init__(self, device_type, device_id, guid, bus_instance=None, affinity_devs=None):
        self.device_type = device_type
        self.device_id = device_id
        self.guid = guid
        self.bus_instance = bus_instance
        self.affinity_devs = affinity_devs or []


class FakeDomain:
    """Minimal libvirt domain used by the VM create/delete flows."""

    def __init__(self, name="vm-1", uuid="uuid-1", state=(1,), xml=VM_XML_WITH_UB, active=True):
        self._name = name
        self._uuid = uuid
        self._state = state
        self._xml = xml
        self._active = active
        self.created = False
        self.destroyed = False
        self.undefine_flags = []

    def name(self):
        return self._name

    def UUIDString(self):
        return self._uuid

    def state(self):
        return self._state

    def XMLDesc(self, *args):
        return self._xml

    def isActive(self):
        return self._active

    def create(self):
        self.created = True

    def destroy(self):
        self.destroyed = True

    def undefineFlags(self, flags):
        self.undefine_flags.append(flags)


class FailingCreateDomain(FakeDomain):
    def create(self):
        raise RuntimeError("domain start failed")


class FakeConnection:
    """Minimal libvirt connection returned by the patched ``libvirt.open``."""

    def __init__(self, domain=None, define_domain=None):
        self._domain = domain
        self._define_domain = define_domain
        self.closed = False
        self.defined_xml = None

    def lookupByName(self, name):
        if self._domain is None:
            raise RuntimeError("domain not found")
        return self._domain

    def defineXML(self, xml):
        self.defined_xml = xml
        return self._define_domain

    def close(self):
        self.closed = True


def make_ns(**overrides):
    data = {
        "tgt_eid": "eid",
        "tgt_nqn": "nqn",
        "ns_uuid": "uuid",
        "namespace_id": 1,
        "ns_dev_path": "/dev/nvme0n1",
        "ns_size": 1024,
        "lba_format": 512,
    }
    data.update(overrides)
    return SimpleNamespace(**data)


def make_alloc_result(name="vol", namespaces=None):
    return SimpleNamespace(
        name=name,
        strategy=2,
        namespaces=[make_ns()] if namespaces is None else namespaces,
    )


@pytest.fixture
def client():
    return TestClient(create_app(), raise_server_exceptions=False)


ALLOC_BODY = {"name": "vol", "ns_size_gb": 1, "ns_num": 1, "lba_format": 512, "strategy": 2}


# ---------------------------------------------------------------------------
# VM create: API -> PrepareVmXml -> BindNpu -> BuildAndStart -> VMClient/libvirt
# ---------------------------------------------------------------------------
class TestVmCreateFlow:
    @patch("ub_device_manager.domain.vm.vm_smbios.VmSmbios.get_type131")
    @patch("ubse.ubs_engine_npu.query_uba_tid_size", return_value=(11, 22, 33))
    @patch("ubse.ubs_engine_npu.alloc_devices", return_value=("bus-1", None))
    @patch("ubse.ubs_engine_npu.get_host_ub_devices")
    @patch("libvirt.open")
    async def test_create_vm_with_npu_binding(self, mock_open, mock_get, mock_alloc, mock_query, mock_smbios, client):
        from ub_device_manager.domain.vm.vm_smbios import SmbiosType131
        mock_smbios.return_value = SmbiosType131(0, 65535, 255, 1, 16383, 3)
        mock_get.return_value = [FakeUbseDevice("NPU", "0-1", "guid-0-1")]
        conn = FakeConnection(define_domain=FakeDomain())
        mock_open.return_value = conn

        response = client.post(
            "/vm/create",
            json={"upi": "0x1", "xml_text": VM_XML, "npu_ids": ["0-1"]},
        )

        assert response.status_code == 200
        assert response.json()["msg"] == "Create VM successfully"
        # The UB controller/hostdev were injected by the real VmXmlBuilder.
        assert "ubmem_vmmu" in conn.defined_xml
        # The hex prefix was stripped before calling ubse and the right device was allocated.
        assert mock_alloc.call_args.kwargs["upi"] == "1"
        assert mock_alloc.call_args.kwargs["device_list"] == [{"device_id": "0-1", "device_type": "NPU"}]
        assert mock_query.call_args.kwargs["bus_instance_guid"] == "bus-1"

    @patch("libvirt.open")
    async def test_create_vm_without_npu_skips_binding(self, mock_open, client):
        conn = FakeConnection(define_domain=FakeDomain())
        mock_open.return_value = conn

        response = client.post("/vm/create", json={"upi": "0x1", "xml_text": VM_XML})

        assert response.status_code == 200
        assert conn.defined_xml == VM_XML

    @patch("ub_device_manager.domain.vm.vm_smbios.VmSmbios.get_type131")
    @patch("ubse.ubs_engine_npu.query_uba_tid_size", return_value=(11, 22, 33))
    @patch("ubse.ubs_engine_npu.alloc_devices", return_value=("bus-1", None))
    @patch("ubse.ubs_engine_npu.get_host_ub_devices")
    @patch("libvirt.open")
    async def test_create_vm_with_count_and_nic(self, mock_open, mock_get, mock_alloc, mock_query, mock_smbios, client):
        from ub_device_manager.domain.vm.vm_smbios import SmbiosType131
        mock_smbios.return_value = SmbiosType131(0, 65535, 255, 1, 16383, 3)
        mock_get.return_value = [
            FakeUbseDevice("NPU", "0-1", "guid-0-1", affinity_devs=[AffinityDeviceInfo(device_id="1-1", device_type="NIC_VFE")]),
            FakeUbseDevice("NIC_VFE", "1-1", "guid-1-1"),
        ]
        conn = FakeConnection(define_domain=FakeDomain())
        mock_open.return_value = conn

        response = client.post(
            "/vm/create",
            json={"upi": "0x1", "xml_text": VM_XML, "npu_count": 1, "need_nic": True},
        )

        assert response.status_code == 200
        assert mock_alloc.call_args.kwargs["device_list"] == [
            {"device_id": "0-1", "device_type": "NPU"},
            {"device_id": "1-1", "device_type": "NIC_VFE"},
        ]


# ---------------------------------------------------------------------------
# VM delete: API -> DeleteVm -> UnbindNpu -> VMClient/libvirt + NpuClient/ubse
# ---------------------------------------------------------------------------
class TestVmDeleteFlow:
    @patch("ubse.ubs_engine_npu.free_devices")
    @patch("ubse.ubs_engine_npu.get_host_ub_devices")
    @patch("libvirt.open")
    async def test_delete_vm_unbinds_devices(self, mock_open, mock_get, mock_free, client):
        domain = FakeDomain()
        mock_open.return_value = FakeConnection(domain=domain)
        mock_get.return_value = [FakeUbseDevice("NPU", "0-1", "guid-0-1", bus_instance="bus-1")]

        response = client.delete("/vm/vm-1")

        assert response.status_code == 200
        assert domain.destroyed is True
        assert domain.undefine_flags
        mock_free.assert_called_once_with(
            bus_instance_guid="bus-1",
            device_list=[{"device_id": "0-1", "device_type": "NPU"}],
        )

    @patch("ubse.ubs_engine_npu.free_devices")
    @patch("ubse.ubs_engine_npu.get_host_ub_devices", return_value=[])
    @patch("libvirt.open")
    async def test_delete_plain_vm_does_not_unbind(self, mock_open, mock_get, mock_free, client):
        # No UB controller in the XML, so there is no bus GUID to unbind.
        domain = FakeDomain(xml="<domain><devices/></domain>")
        mock_open.return_value = FakeConnection(domain=domain)

        response = client.delete("/vm/vm-1")

        assert response.status_code == 200
        mock_free.assert_not_called()

    @patch("ubse.ubs_engine_npu.free_devices")
    @patch("ubse.ubs_engine_npu.get_host_ub_devices", return_value=[])
    @patch("libvirt.open")
    async def test_delete_vm_with_unknown_bus_guid_returns_548(self, mock_open, mock_get, mock_free, client):
        domain = FakeDomain()  # XML references bus-1, but ubse reports no such device
        mock_open.return_value = FakeConnection(domain=domain)

        response = client.delete("/vm/vm-1")

        assert response.status_code == 548
        mock_free.assert_not_called()


# ---------------------------------------------------------------------------
# NPU endpoints: API -> npu_tasks -> NpuClient/ubse
# ---------------------------------------------------------------------------
class TestNpuEndpointsFlow:
    @patch("ubse.ubs_engine_npu.get_host_ub_devices")
    async def test_list_npu(self, mock_get, client):
        mock_get.return_value = [
            FakeUbseDevice("NPU", "0-1", "g1"),
            FakeUbseDevice("NIC_VFE", "1-1", "g2"),
        ]

        response = client.get("/npu")

        assert response.status_code == 200
        assert response.json() == [{"id": "0-1", "guid": "g1", "bus_guid": None}]

    @patch("ubse.ubs_engine_npu.query_uba_tid_size", return_value=(11, 22, 33))
    @patch("ubse.ubs_engine_npu.alloc_devices", return_value=("bus-1", None))
    @patch("ubse.ubs_engine_npu.get_host_ub_devices")
    async def test_bind_npu(self, mock_get, mock_alloc, mock_query, client):
        mock_get.return_value = [FakeUbseDevice("NPU", "1-1", "g1")]

        response = client.post("/npu/bind", json={"upi": "0x1", "count": 1})

        assert response.status_code == 200
        body = response.json()
        assert body["bus_guid"] == "bus-1"
        assert (body["tid"], body["uba"], body["size"]) == (11, 22, 33)
        assert body["devices"][0]["id"] == "1-1"

    @patch("ubse.ubs_engine_npu.free_devices")
    @patch("ubse.ubs_engine_npu.get_host_ub_devices")
    async def test_unbind_npu(self, mock_get, mock_free, client):
        mock_get.return_value = [FakeUbseDevice("NPU", "0-1", "g", bus_instance="bus-1")]

        response = client.post("/npu/unbind", json={"bus_guid": "bus-1"})

        assert response.status_code == 200
        mock_free.assert_called_once_with(
            bus_instance_guid="bus-1",
            device_list=[{"device_id": "0-1", "device_type": "NPU"}],
        )


# ---------------------------------------------------------------------------
# SSU endpoints: API -> ssu_tasks -> SsuClient/ubse
# ---------------------------------------------------------------------------
class TestSsuFlow:
    @patch("ubse.ubs_engine_ssu.ubs_ssu_space_alloc")
    async def test_alloc_ssu(self, mock_alloc, client):
        mock_alloc.return_value = make_alloc_result("vol")

        response = client.post("/ssu/alloc", json=ALLOC_BODY)

        assert response.status_code == 200
        assert response.json()["name"] == "vol"
        assert response.json()["namespaces"][0]["ns_dev_path"] == "/dev/nvme0n1"

    @patch("ubse.ubs_engine_ssu.ubs_ssu_alloc_info_list")
    async def test_list_ssu(self, mock_list, client):
        mock_list.return_value = [make_alloc_result("a"), make_alloc_result("b")]

        response = client.get("/ssu")

        assert response.status_code == 200
        assert [item["name"] for item in response.json()] == ["a", "b"]

    @patch("ubse.ubs_engine_ssu.ubs_ssu_alloc_info_list", return_value=[make_alloc_result("vol")])
    async def test_show_ssu(self, mock_list, client):
        response = client.get("/ssu/vol")

        assert response.status_code == 200
        assert response.json()["name"] == "vol"

    @patch("ubse.ubs_engine_ssu.ubs_ssu_space_free")
    async def test_free_ssu(self, mock_free, client):
        response = client.delete("/ssu/vol")

        assert response.status_code == 200
        mock_free.assert_called_once_with("vol")


# ---------------------------------------------------------------------------
# Error paths mapped across the whole stack
# ---------------------------------------------------------------------------
class TestErrorFlows:
    @patch("ubse.ubs_engine_npu.get_host_ub_devices")
    async def test_npu_not_found_returns_545(self, mock_get, client):
        mock_get.return_value = [FakeUbseDevice("NPU", "0-1", "g")]

        response = client.post(
            "/vm/create",
            json={"upi": "0x1", "xml_text": VM_XML, "npu_ids": ["9-9"]},
        )

        assert response.status_code == 545

    @patch("libvirt.open", side_effect=RuntimeError("no daemon"))
    async def test_libvirt_connection_failure_returns_532(self, mock_open, client):
        response = client.post("/vm/create", json={"upi": "0x1", "xml_text": VM_XML})

        assert response.status_code == 532

    @patch("ub_device_manager.domain.vm.vm_smbios.VmSmbios.get_type131")
    @patch("ubse.ubs_engine_npu.free_devices")
    @patch("ubse.ubs_engine_npu.query_uba_tid_size", return_value=(1, 2, 3))
    @patch("ubse.ubs_engine_npu.alloc_devices", return_value=("bus-1", None))
    @patch("ubse.ubs_engine_npu.get_host_ub_devices")
    @patch("libvirt.open")
    async def test_vm_start_failure_rolls_back_devices(self, mock_open, mock_get, mock_alloc, mock_query, mock_free, mock_smbios, client):
        from ub_device_manager.domain.vm.vm_smbios import SmbiosType131
        mock_smbios.return_value = SmbiosType131(0, 65535, 255, 1, 16383, 3)
        mock_get.return_value = [FakeUbseDevice("NPU", "0-1", "guid-0-1")]
        mock_open.return_value = FakeConnection(define_domain=FailingCreateDomain())

        response = client.post(
            "/vm/create",
            json={"upi": "0x1", "xml_text": VM_XML, "npu_ids": ["0-1"]},
        )

        assert response.status_code == 544
        mock_free.assert_called_once_with(
            bus_instance_guid="bus-1",
            device_list=[{"device_id": "0-1", "device_type": "NPU"}],
        )

    @patch("ubse.ubs_engine_ssu.ubs_ssu_alloc_info_list", return_value=[])
    async def test_show_missing_ssu_returns_404(self, mock_list, client):
        response = client.get("/ssu/missing")

        assert response.status_code == 404
