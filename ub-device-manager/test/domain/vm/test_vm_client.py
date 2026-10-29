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
"""Unit tests for :mod:`ub_device_manager.domain.vm.vm_client`."""
from unittest.mock import AsyncMock, patch

import pytest

from ub_device_manager.domain.vm.vm_client import ControllerInfo, VMClient, VmInfo
from ub_device_manager.exceptions import (
    GetVmInfoFailed,
    LibvirtConnectionFailed,
    VmDefineStartFailed,
    VmDeletionFailed,
)

UB_XML = """
<domain type='kvm'>
  <devices>
    <controller type='pci' index='1' model='pci-root'/>
    <controller type='ub' index='0' model='ubc'>
      <source><businstance guid='bus-guid'/></source>
    </controller>
  </devices>
</domain>
"""


class FakeDomain:
    def __init__(self, name="vm-1", uuid="uuid-1", state=(1,), xml=UB_XML, active=True):
        self._name = name
        self._uuid = uuid
        self._state = state
        self._xml = xml
        self._active = active
        self.destroyed = False
        self.undefine_flags = []
        self.created = False

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

    def destroy(self):
        self.destroyed = True

    def undefineFlags(self, flags):
        self.undefine_flags.append(flags)

    def create(self):
        self.created = True


class FakeConnection:
    def __init__(self, domain=None, all_domains=None, define_domain=None):
        self._domain = domain
        self._all_domains = all_domains if all_domains is not None else []
        self._define_domain = define_domain
        self.closed = False
        self.looked_up = []
        self.defined_xml = None

    def lookupByName(self, name):
        self.looked_up.append(name)
        if self._domain is None:
            raise RuntimeError("domain not found")
        return self._domain

    def listAllDomains(self, flags=0):
        return self._all_domains

    def defineXML(self, xml):
        self.defined_xml = xml
        return self._define_domain

    def close(self):
        self.closed = True


class TestConnect:
    @patch("libvirt.open")
    async def test_connect_returns_connection(self, mock_open):
        conn = FakeConnection()
        mock_open.return_value = conn

        assert await VMClient().connect() is conn
        mock_open.assert_called_once_with("qemu:///system")

    @patch("libvirt.open")
    async def test_connect_failure_is_wrapped(self, mock_open):
        mock_open.side_effect = RuntimeError("no daemon")

        with pytest.raises(LibvirtConnectionFailed):
            await VMClient().connect()


class TestGetVm:
    async def test_returns_vm_info_and_parses_controllers(self):
        info = await VMClient().get_vm(FakeConnection(domain=FakeDomain()), "vm-1")

        assert info == VmInfo(
            name="vm-1",
            uuid="uuid-1",
            state="running",
            controllers=[ControllerInfo(ctrl_type="ub", index="0", model="ubc", source={"businstance_guid": "bus-guid"})],
        )

    async def test_failure_is_wrapped(self):
        with pytest.raises(GetVmInfoFailed):
            await VMClient().get_vm(FakeConnection(domain=None), "missing")


class TestGetAllVms:
    async def test_returns_all_domains(self):
        conn = FakeConnection(all_domains=[FakeDomain(name="a"), FakeDomain(name="b")])

        vms = await VMClient().get_all_vms(conn)

        assert [vm.name for vm in vms] == ["a", "b"]

    async def test_failure_is_wrapped(self):
        class BrokenConnection(FakeConnection):
            def listAllDomains(self, flags=0):
                raise RuntimeError("boom")

        with pytest.raises(GetVmInfoFailed):
            await VMClient().get_all_vms(BrokenConnection())


class TestDeleteVm:
    async def test_destroys_active_domain_and_undefines(self):
        domain = FakeDomain(active=True)

        await VMClient().delete_vm(FakeConnection(domain=domain), "vm-1")

        assert domain.destroyed is True
        assert domain.undefine_flags == [2]  # libvirt.VIR_DOMAIN_UNDEFINE_NVRAM

    async def test_inactive_domain_is_not_destroyed(self):
        domain = FakeDomain(active=False)

        await VMClient().delete_vm(FakeConnection(domain=domain), "vm-1")

        assert domain.destroyed is False
        assert domain.undefine_flags

    async def test_failure_is_wrapped(self):
        with pytest.raises(VmDeletionFailed):
            await VMClient().delete_vm(FakeConnection(domain=None), "missing")


class TestParseControllers:
    @pytest.mark.parametrize(
        ("xml", "expected"),
        [
            ("<domain><devices/></domain>", []),
            (
                "<domain><devices><controller type='ub' index='2' model='ubc'/></devices></domain>",
                [ControllerInfo(ctrl_type="ub", index="2", model="ubc", source={})],
            ),
            (
                "<domain><devices><controller type='pci' index='0'/></devices></domain>",
                [],
            ),
        ],
    )
    async def test_parses_only_ub_controllers(self, xml, expected):
        assert await VMClient._parse_controllers(xml) == expected


class TestStateToString:
    @pytest.mark.parametrize(
        ("state", "expected"),
        [
            (0, "no state"),
            (1, "running"),
            (2, "blocked"),
            (3, "paused"),
            (4, "shutdown"),
            (5, "shut off"),
            (6, "crashed"),
            (7, "suspended"),
            (99, "unknown(99)"),
        ],
    )
    async def test_maps_state_codes(self, state, expected):
        assert await VMClient._state_to_str(state) == expected


class TestDisconnect:
    async def test_closes_connection(self):
        conn = FakeConnection()

        await VMClient.disconnect(conn)

        assert conn.closed is True

    async def test_none_connection_is_ignored(self):
        await VMClient.disconnect(None)

    async def test_close_error_is_swallowed(self):
        class BrokenConnection(FakeConnection):
            def close(self):
                raise RuntimeError("boom")

        await VMClient.disconnect(BrokenConnection())


class TestDefineAndStart:
    @patch.object(VMClient, "connect", new_callable=AsyncMock)
    async def test_defines_and_starts_and_disconnects(self, mock_connect):
        domain = FakeDomain()
        conn = FakeConnection(define_domain=domain)
        mock_connect.return_value = conn

        await VMClient().define_and_start("<domain/>")

        assert conn.defined_xml == "<domain/>"
        assert domain.created is True
        assert conn.closed is True

    @patch.object(VMClient, "connect", new_callable=AsyncMock)
    async def test_missing_domain_raises_and_disconnects(self, mock_connect):
        conn = FakeConnection(define_domain=None)
        mock_connect.return_value = conn

        with pytest.raises(VmDefineStartFailed, match="Failed to define VM domain"):
            await VMClient().define_and_start("<domain/>")

        assert conn.closed is True

    @patch.object(VMClient, "connect", new_callable=AsyncMock)
    async def test_start_failure_undefines_and_reraises(self, mock_connect):
        domain = FakeDomain()

        def broken_create():
            raise RuntimeError("start failed")

        domain.create = broken_create
        conn = FakeConnection(define_domain=domain)
        mock_connect.return_value = conn

        with pytest.raises(VmDefineStartFailed):
            await VMClient().define_and_start("<domain/>")

        assert domain.undefine_flags == [2]  # libvirt.VIR_DOMAIN_UNDEFINE_NVRAM
        assert conn.closed is True
