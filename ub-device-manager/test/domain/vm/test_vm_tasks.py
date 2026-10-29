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
"""Unit tests for :mod:`ub_device_manager.domain.vm.vm_tasks`."""
from unittest.mock import AsyncMock, patch

import pytest
from test.helpers import run_task

from ub_device_manager.app.models import BindNpuDeviceResult, BoundUbDevice, CreateVmRequest
from ub_device_manager.constants import (
    BIND_REQUEST_CONTEXT_KEY,
    BIND_RESULT_CONTEXT_KEY,
    CREATE_VM_REQUEST_CONTEXT_KEY,
    DELETE_VM_CONTEXT_KEY,
    UNBIND_REQUEST_CONTEXT_KEY,
    VM_XML_CONTEXT_KEY,
)
from ub_device_manager.domain.npu.npu_client import NpuClient
from ub_device_manager.domain.vm import vm_tasks
from ub_device_manager.domain.vm.vm_client import ControllerInfo, VMClient, VmInfo
from ub_device_manager.domain.vm.vm_tasks import (
    BuildAndStartVmTask,
    DeleteVmTask,
    GetAllVmTask,
    GetVmsListResp,
    PrepareVmXmlTask,
)
from ub_device_manager.domain.vm.vm_xml_builder import VmXmlBuilder
from ub_device_manager.exceptions import InvalidCreateVmRequest, VmXmlBuildFailed, VmXmlTooLarge

VALID_XML = "<domain><devices/></domain>"


def make_request(**overrides):
    payload = {"upi": "0x1", "xml_text": VALID_XML}
    payload.update(overrides)
    return CreateVmRequest(**payload)


def make_bind_result(bus_guid="bus-1"):
    return BindNpuDeviceResult(
        tid=1,
        uba=2,
        size=3,
        bus_guid=bus_guid,
        devices=[BoundUbDevice(id="1-1", guid="g1", type="NPU")],
    )


def make_vm_info():
    return VmInfo(
        name="vm-1",
        uuid="u",
        state="running",
        controllers=[ControllerInfo(ctrl_type="ub", index="0", model="ubc", source={"businstance_guid": "bus-9"})],
    )


class TestGetAllVmTask:
    @patch.object(VMClient, "disconnect", new_callable=AsyncMock)
    @patch.object(VMClient, "get_all_vms", new_callable=AsyncMock)
    @patch.object(VMClient, "connect", new_callable=AsyncMock)
    async def test_returns_vm_list_and_disconnects(self, mock_connect, mock_get_all, mock_disconnect):
        mock_connect.return_value = "conn"
        mock_get_all.return_value = [VmInfo(name="a", uuid="u", state="running")]

        result = await GetAllVmTask().execute()

        assert [vm.name for vm in result.vms] == ["a"]
        mock_disconnect.assert_awaited_once_with(conn="conn")


class TestPrepareVmXmlTaskValidation:
    async def test_missing_request_raises(self):
        with pytest.raises(InvalidCreateVmRequest, match="not found in task context"):
            await run_task(PrepareVmXmlTask())

    async def test_xml_text_and_xml_path_are_mutually_exclusive(self):
        request = make_request(xml_path="/tmp/x.xml")

        with pytest.raises(InvalidCreateVmRequest, match="exactly one"):
            await run_task(PrepareVmXmlTask(), {CREATE_VM_REQUEST_CONTEXT_KEY: request})

    async def test_neither_xml_text_nor_path_raises(self):
        request = CreateVmRequest.model_construct(upi="0x1", xml_text=None, xml_path=None, npu_ids=None, npu_count=None)

        with pytest.raises(InvalidCreateVmRequest, match="exactly one"):
            await run_task(PrepareVmXmlTask(), {CREATE_VM_REQUEST_CONTEXT_KEY: request})

    async def test_npu_ids_and_count_are_mutually_exclusive(self):
        request = make_request(npu_ids=["1-1"], npu_count=2)

        with pytest.raises(InvalidCreateVmRequest, match="mutually exclusive"):
            await run_task(PrepareVmXmlTask(), {CREATE_VM_REQUEST_CONTEXT_KEY: request})

    async def test_duplicate_npu_ids_are_rejected(self):
        request = make_request(npu_ids=["1-1", "1-1"])

        with pytest.raises(InvalidCreateVmRequest, match="duplicated"):
            await run_task(PrepareVmXmlTask(), {CREATE_VM_REQUEST_CONTEXT_KEY: request})

    async def test_empty_xml_text_is_rejected(self):
        request = make_request(xml_text="   ")

        with pytest.raises(InvalidCreateVmRequest, match="xml_text is empty"):
            await run_task(PrepareVmXmlTask(), {CREATE_VM_REQUEST_CONTEXT_KEY: request})


class TestPrepareVmXmlTaskExecute:
    async def test_loads_xml_into_context(self):
        context = await run_task(PrepareVmXmlTask(), {CREATE_VM_REQUEST_CONTEXT_KEY: make_request()})

        assert context.get(VM_XML_CONTEXT_KEY) == VALID_XML

    async def test_sets_bind_request_for_npu_ids(self):
        context = await run_task(
            PrepareVmXmlTask(),
            {CREATE_VM_REQUEST_CONTEXT_KEY: make_request(npu_ids=["1-1"])},
        )

        assert context.get(BIND_REQUEST_CONTEXT_KEY).ids == ["1-1"]

    async def test_omits_bind_request_without_npus(self):
        context = await run_task(PrepareVmXmlTask(), {CREATE_VM_REQUEST_CONTEXT_KEY: make_request()})

        assert not context.get(BIND_REQUEST_CONTEXT_KEY)

    async def test_missing_xml_path_is_rejected(self):
        with pytest.raises(InvalidCreateVmRequest, match="invalid xml input"):
            await run_task(
                PrepareVmXmlTask(),
                {CREATE_VM_REQUEST_CONTEXT_KEY: make_request(xml_text=None, xml_path="/nope/missing.xml")},
            )

    async def test_reads_xml_from_file(self, tmp_path):
        xml_file = tmp_path / "domain.xml"
        xml_file.write_text(VALID_XML, encoding="utf-8")

        context = await run_task(
            PrepareVmXmlTask(),
            {CREATE_VM_REQUEST_CONTEXT_KEY: make_request(xml_text=None, xml_path=str(xml_file))},
        )

        assert context.get(VM_XML_CONTEXT_KEY) == VALID_XML

    @patch.dict(vm_tasks.CONFIG, {"vm": {"max_xml_size": 8}})
    async def test_oversized_xml_text_is_rejected(self):
        with pytest.raises(VmXmlTooLarge):
            await run_task(PrepareVmXmlTask(), {CREATE_VM_REQUEST_CONTEXT_KEY: make_request()})

    @patch.dict(vm_tasks.CONFIG, {"vm": {"max_xml_size": 8}})
    async def test_oversized_xml_file_is_rejected(self, tmp_path):
        xml_file = tmp_path / "big.xml"
        xml_file.write_text(VALID_XML, encoding="utf-8")

        with pytest.raises(VmXmlTooLarge):
            await run_task(
                PrepareVmXmlTask(),
                {CREATE_VM_REQUEST_CONTEXT_KEY: make_request(xml_text=None, xml_path=str(xml_file))},
            )

    async def test_invalid_xml_is_rejected(self):
        with pytest.raises(InvalidCreateVmRequest, match="invalid xml input"):
            await run_task(
                PrepareVmXmlTask(),
                {CREATE_VM_REQUEST_CONTEXT_KEY: make_request(xml_text="<controller/>")},
            )


class TestBuildAndStartVmTask:
    async def test_missing_xml_raises(self):
        with pytest.raises(VmXmlBuildFailed):
            await run_task(BuildAndStartVmTask())

    @patch.object(VMClient, "define_and_start", new_callable=AsyncMock)
    async def test_starts_vm_without_bound_devices(self, mock_define):
        context = await run_task(BuildAndStartVmTask(), {VM_XML_CONTEXT_KEY: VALID_XML})

        mock_define.assert_awaited_once_with(VALID_XML)
        assert not context.get(BIND_RESULT_CONTEXT_KEY)

    @patch("ub_device_manager.domain.vm.vm_smbios.VmSmbios.get_type131", return_value=None)
    @patch.object(VmXmlBuilder, "build", return_value="final-xml")
    @patch.object(VMClient, "define_and_start", new_callable=AsyncMock)
    async def test_builds_final_xml_when_devices_are_bound(self, mock_define, mock_build, mock_read):
        await run_task(BuildAndStartVmTask(), {VM_XML_CONTEXT_KEY: VALID_XML, BIND_RESULT_CONTEXT_KEY: make_bind_result()})

        mock_define.assert_awaited_once_with("final-xml")

    @patch("ub_device_manager.domain.vm.vm_smbios.VmSmbios.get_type131", return_value=None)
    @patch.object(NpuClient, "free_devices", new_callable=AsyncMock)
    @patch.object(VMClient, "define_and_start", new_callable=AsyncMock, side_effect=RuntimeError("start failed"))
    async def test_rolls_back_devices_when_start_fails(self, mock_define, mock_free, mock_read):
        with pytest.raises(RuntimeError, match="start failed"):
            await run_task(BuildAndStartVmTask(), {VM_XML_CONTEXT_KEY: VALID_XML, BIND_RESULT_CONTEXT_KEY: make_bind_result()})

        mock_free.assert_awaited_once_with("bus-1", [{"device_id": "1-1", "device_type": "NPU"}])

    @patch("ub_device_manager.domain.vm.vm_smbios.VmSmbios.get_type131", return_value=None)
    @patch.object(NpuClient, "free_devices", new_callable=AsyncMock, side_effect=RuntimeError("rollback failed"))
    @patch.object(VMClient, "define_and_start", new_callable=AsyncMock, side_effect=RuntimeError("start failed"))
    async def test_rollback_failure_does_not_mask_original_error(self, mock_define, mock_free, mock_read):
        with pytest.raises(RuntimeError, match="start failed"):
            await run_task(BuildAndStartVmTask(), {VM_XML_CONTEXT_KEY: VALID_XML, BIND_RESULT_CONTEXT_KEY: make_bind_result()})


class TestDeleteVmTask:
    @patch.object(VMClient, "disconnect", new_callable=AsyncMock)
    @patch.object(VMClient, "delete_vm", new_callable=AsyncMock)
    @patch.object(VMClient, "get_vm", new_callable=AsyncMock)
    @patch.object(VMClient, "connect", new_callable=AsyncMock)
    async def test_sets_unbind_guid_from_first_controller(self, mock_connect, mock_get_vm, mock_delete, mock_disconnect):
        mock_connect.return_value = "conn"
        mock_get_vm.return_value = make_vm_info()

        context = await run_task(DeleteVmTask(), {DELETE_VM_CONTEXT_KEY: "vm-1"})

        mock_delete.assert_awaited_once_with(conn="conn", name="vm-1")
        assert context.get(UNBIND_REQUEST_CONTEXT_KEY) == "bus-9"

    @patch.object(VMClient, "disconnect", new_callable=AsyncMock)
    @patch.object(VMClient, "delete_vm", new_callable=AsyncMock)
    @patch.object(VMClient, "get_vm", new_callable=AsyncMock)
    @patch.object(VMClient, "connect", new_callable=AsyncMock)
    async def test_sets_empty_guid_without_controllers(self, mock_connect, mock_get_vm, mock_delete, mock_disconnect):
        mock_connect.return_value = "conn"
        mock_get_vm.return_value = VmInfo(name="vm-1", uuid="u", state="shut off", controllers=[])

        context = await run_task(DeleteVmTask(), {DELETE_VM_CONTEXT_KEY: "vm-1"})

        assert context.get(UNBIND_REQUEST_CONTEXT_KEY) == ""


async def test_get_vms_list_response_wraps_vms():
    response = GetVmsListResp(vms=[VmInfo(name="a", uuid="u", state="running")])

    assert response.vms[0].name == "a"

class TestVmSmbiosIntegration:
    @patch('ub_device_manager.domain.vm.vm_smbios.VmSmbios.get_type131')
    @patch.object(VMClient, 'define_and_start', new_callable=AsyncMock)
    async def test_only_npu_vm_queries_smbios(self, mock_define, mock_read):
        from ub_device_manager.domain.vm.vm_smbios import SmbiosType131
        mock_read.return_value = SmbiosType131(0, 1, 2, 1, 3, 4)
        xml = '<domain><os><type>hvm</type></os><devices/></domain>'
        await run_task(BuildAndStartVmTask(), {VM_XML_CONTEXT_KEY: xml})
        mock_read.assert_not_called()
        await run_task(BuildAndStartVmTask(), {
            VM_XML_CONTEXT_KEY: xml, BIND_RESULT_CONTEXT_KEY: make_bind_result(),
        })
        mock_read.assert_called_once()
        assert 'type131' in mock_define.await_args.args[0]


class TestType131FailClosed:
    @patch('ub_device_manager.domain.vm.vm_smbios.VmSmbios.get_type131',
           side_effect=VmXmlBuildFailed('cannot read host SMBIOS Type 131'))
    @patch.object(NpuClient, 'free_devices', new_callable=AsyncMock)
    @patch.object(VMClient, 'define_and_start', new_callable=AsyncMock)
    async def test_missing_type131_prevents_vm_start_and_rolls_back(self, mock_define, mock_free, mock_read):
        with pytest.raises(VmXmlBuildFailed, match='cannot read host SMBIOS Type 131'):
            await run_task(BuildAndStartVmTask(), {
                VM_XML_CONTEXT_KEY: VALID_XML,
                BIND_RESULT_CONTEXT_KEY: make_bind_result(),
            })
        mock_define.assert_not_awaited()
        mock_free.assert_awaited_once_with('bus-1', [{'device_id': '1-1', 'device_type': 'NPU'}])
