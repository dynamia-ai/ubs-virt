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
"""Unit tests for :mod:`ub_device_manager.domain.npu.npu_client`."""
import subprocess
from unittest.mock import patch

import pytest

from ub_device_manager.domain.npu import npu_client as npu_client_module
from ub_device_manager.domain.npu.npu_client import (
    AffinityDeviceInfo,
    AllocDevicesParams,
    DeviceRef,
    NpuClient,
    ProductType,
    UbDeviceInfo,
)


class FakeHostDevice:
    def __init__(self, device_type, device_id, guid, bus_instance=None, affinity_devs=None):
        self.device_type = device_type
        self.device_id = device_id
        self.guid = guid
        self.bus_instance = bus_instance
        self.affinity_devs = affinity_devs or []


class TestLogCallbackRegistration:
    @patch.object(npu_client_module, "_ubse_log_registered", False)
    @patch("ubse.ubs_engine_log.ubs_engine_log_callback_register")
    async def test_callback_is_registered_only_once(self, mock_register):
        npu_client_module._register_ubse_log_callback()
        npu_client_module._register_ubse_log_callback()

        assert mock_register.call_count == 1


class TestGetHostUbDevices:
    @patch("ubse.ubs_engine_npu.get_host_ub_devices")
    async def test_filters_and_converts_inventory(self, mock_get):
        mock_get.return_value = [
            FakeHostDevice("NPU", "0-1", "g1", bus_instance="bus", affinity_devs=[]),
            FakeHostDevice("NIC_VFE", "1-1", "g2"),
            FakeHostDevice("GPU", "2-1", "g3"),
        ]

        devices = await NpuClient.get_host_ub_devices()

        assert [device.id for device in devices] == ["0-1", "1-1"]
        assert devices[0].bus_guid == "bus"
        assert devices[0].type == "NPU"


class TestProductType:
    @pytest.mark.parametrize("raw, expected", [
        (0, ProductType.SERVER), (1, ProductType.POD_16_1825), (2, ProductType.POD_32_1825),
    ])
    @patch("ubse.ubs_engine_npu.get_product_type")
    async def test_maps_ubse_type(self, mock_get, raw, expected):
        mock_get.return_value = raw
        assert await NpuClient.get_product_type() is expected

    @patch("ubse.ubs_engine_npu.get_product_type", return_value=99)
    async def test_unknown_type_fails(self, mock_get):
        with pytest.raises(RuntimeError, match="Unknown UBSE product type"):
            await NpuClient.get_product_type()

    @patch("ubse.ubs_engine_npu.get_product_type", return_value=True)
    async def test_boolean_type_fails(self, mock_get):
        with pytest.raises(RuntimeError, match="Unknown UBSE product type"):
            await NpuClient.get_product_type()


class TestConfigureNics:
    @pytest.mark.parametrize("option, value", [
        ("-m", "02:fb:c0:da:88:b3"), ("-c", None),
    ])
    @patch.object(npu_client_module.subprocess, "run")
    def test_nic_command_uses_noninteractive_sudo(self, mock_run, option, value):
        NpuClient._run_nic_command("eth0", "32", option, value)

        command = ["/usr/bin/sudo", "-n", "/usr/sbin/hinicadm5", "vfconfig",
                   "-i", "eth0", "-f", "32", option]
        if value is not None:
            command.append(value)
        mock_run.assert_called_once_with(
            command,
            check=True, capture_output=True, text=True, timeout=npu_client_module.NIC_COMMAND_TIMEOUT,
        )

    @pytest.mark.parametrize("option, value, expected_message", [
        ("-m", "02:fb:c0:da:88:b3", "VF MAC configuration command succeeded"),
        ("-c", None, "VF MAC/SVLAN clear command succeeded"),
    ])
    @patch.object(npu_client_module.logger, "info")
    @patch.object(npu_client_module.subprocess, "run")
    def test_nic_command_logs_success_result(self, mock_run, mock_log, option, value, expected_message):
        mock_run.return_value = subprocess.CompletedProcess(
            args=["hinicadm5", "vfconfig"], returncode=0,
            stdout="will take effect after reloading the driver", stderr="",
        )

        NpuClient._run_nic_command("eth0", "32", option, value)

        log_args = mock_log.call_args.args
        assert expected_message in log_args[0]
        assert log_args[1:3] == ("eth0", "32")
        if option == "-m":
            assert log_args[3] == value
        assert log_args[-3:] == (0, "will take effect after reloading the driver", "")

    @patch.object(npu_client_module.logger, "info")
    @patch.object(npu_client_module.subprocess, "run")
    def test_failed_nic_command_does_not_log_success(self, mock_run, mock_log):
        mock_run.side_effect = subprocess.CalledProcessError(250, ["hinicadm5", "vfconfig"])

        with pytest.raises(subprocess.CalledProcessError):
            NpuClient._run_nic_command("eth0", "32", "-m", "02:fb:c0:da:88:b3")

        mock_log.assert_not_called()

    @pytest.mark.parametrize("interface, function_id, option, value", [
        ("eth0;id", "32", "-m", "02:fb:c0:da:88:b3"),
        ("eth0", "32;id", "-m", "02:fb:c0:da:88:b3"),
        ("eth0", "32", "-m", "ff:fb:c0:da:88:b3"),
        ("eth0", "32", "-s", "0"),
        ("eth0", "32", "-s", "1"),
        ("eth0", "32", "-c", "0"),
        ("eth0", "32", "-x", "1"),
    ])
    @patch.object(npu_client_module.subprocess, "run")
    def test_nic_command_rejects_unapproved_arguments(self, mock_run, interface, function_id, option, value):
        with pytest.raises(ValueError):
            NpuClient._run_nic_command(interface, function_id, option, value)

        mock_run.assert_not_called()

    @patch.object(NpuClient, "_run_nic_command")
    @patch.object(NpuClient, "_find_interface", return_value="eth0")
    async def test_sets_mac_without_svlan(self, mock_find, mock_run):
        configured = await NpuClient.configure_nics([("pfe-guid", "7")])

        assert configured == [("eth0", "7")]
        mock_run.assert_called_once()
        assert mock_run.call_args.args[:3] == ("eth0", "7", "-m")

    @patch.object(NpuClient, "_run_nic_command")
    @patch.object(NpuClient, "_find_interface", return_value="eth0")
    async def test_clears_first_vf_after_second_mac_failure(self, mock_find, mock_run):
        mock_find.side_effect = ["eth0", "eth1"]

        def run(interface, function_id, option, value=None):
            if interface == "eth1" and option == "-m":
                raise RuntimeError("set MAC failed")

        mock_run.side_effect = run
        with pytest.raises(RuntimeError, match="set MAC failed"):
            await NpuClient.configure_nics([("pfe-1", "7"), ("pfe-2", "8")])

        assert mock_run.call_args_list[-1].args == ("eth0", "7", "-c")

    @patch.object(NpuClient, "_run_nic_command")
    async def test_clear_nic_configs_uses_c_in_reverse_order(self, mock_run):
        await NpuClient.clear_nic_configs([("eth0", "7"), ("eth1", "8")])

        assert [call.args for call in mock_run.call_args_list] == [
            ("eth1", "8", "-c"), ("eth0", "7", "-c"),
        ]

    async def test_logs_mac_failure_and_command_output_before_rollback(self):
        events = []
        failure = subprocess.CalledProcessError(
            2, ["hinicadm5", "vfconfig"], output="command output", stderr="invalid MAC"
        )

        async def clear(configured):
            events.append("rollback")

        with patch.object(NpuClient, "_find_interface", return_value="eth0"), \
                patch.object(NpuClient, "_run_nic_command", side_effect=failure), \
                patch.object(NpuClient, "clear_nic_configs", side_effect=clear), \
                patch.object(npu_client_module.logger, "error") as mock_log:
            mock_log.side_effect = lambda *args: events.append("log")
            with pytest.raises(subprocess.CalledProcessError):
                await NpuClient.configure_nics([("pfe-guid", "7")])

        assert events == ["log", "rollback"]
        log_args = mock_log.call_args.args
        assert "step={}" in log_args[0]
        assert log_args[1:5] == ("set MAC", "pfe-guid", "eth0", "7")
        assert "exit_code={}" in log_args[0]
        assert log_args[-3] == 2
        assert log_args[-2:] == ("command output", "invalid MAC")

    @patch.object(NpuClient, "_run_nic_command")
    async def test_clear_failure_logs_exit_code_and_command_output(self, mock_run):
        mock_run.side_effect = subprocess.CalledProcessError(
            250, ["hinicadm5", "vfconfig"], output="tool output", stderr="invalid option"
        )
        with patch.object(npu_client_module.logger, "error") as mock_log:
            await NpuClient.clear_nic_configs([("eth0", "32")])

        log_args = mock_log.call_args.args
        assert log_args[1:3] == ("eth0", "32")
        assert log_args[-3:] == (250, "tool output", "invalid option")


class TestBindDevices:
    def _params(self):
        return AllocDevicesParams(
            upi="0x0f",
            device_list=[DeviceRef(device_id="1-1", device_type="NPU")],
        )

    @patch("ubse.ubs_engine_npu.query_uba_tid_size", return_value=(11, 22, 33))
    @patch("ubse.ubs_engine_npu.alloc_devices")
    async def test_strips_hex_prefix_and_returns_mapping(self, mock_alloc, mock_query):
        mock_alloc.return_value = ("bus-guid", None)

        result = await NpuClient().bind_devices(self._params())

        assert result == ("bus-guid", 11, 22, 33)
        mock_alloc.assert_called_once_with(
            upi="0f",
            bus_guid="",
            device_list=[{"device_id": "1-1", "device_type": "NPU"}],
        )

    @patch("ubse.ubs_engine_npu.query_uba_tid_size", return_value=(1, 2, 3))
    @patch("ubse.ubs_engine_npu.alloc_devices", return_value=("bus", None))
    async def test_keeps_non_prefixed_upi(self, mock_alloc, mock_query):
        await NpuClient().bind_devices(
            AllocDevicesParams(upi="0f", device_list=[DeviceRef(device_id="1-1", device_type="NPU")])
        )

        assert mock_alloc.call_args.kwargs["upi"] == "0f"

    @patch("ubse.ubs_engine_npu.free_devices")
    @patch("ubse.ubs_engine_npu.query_uba_tid_size", side_effect=RuntimeError("query failed"))
    @patch("ubse.ubs_engine_npu.alloc_devices", return_value=("bus-guid", None))
    async def test_releases_devices_when_mapping_query_fails(self, mock_alloc, mock_query, mock_free):
        with pytest.raises(RuntimeError, match="query failed"):
            await NpuClient().bind_devices(self._params())

        mock_free.assert_called_once_with(
            bus_instance_guid="bus-guid",
            device_list=[{"device_id": "1-1", "device_type": "NPU"}],
        )


class TestFreeDevices:
    @patch("ubse.ubs_engine_npu.free_devices")
    async def test_forwards_to_ubse(self, mock_free):
        await NpuClient.free_devices("bus", [{"device_id": "1-1", "device_type": "NPU"}])

        mock_free.assert_called_once_with(
            bus_instance_guid="bus",
            device_list=[{"device_id": "1-1", "device_type": "NPU"}],
        )


async def test_affinity_device_info_defaults():
    info = AffinityDeviceInfo(device_id="1-1", device_type="NIC_VFE")
    assert info.device_id == "1-1"


async def test_ub_device_info_requires_guid():
    assert UbDeviceInfo(id="0-1", type="NPU", guid="g").bus_guid is None


@patch('ubse.ubs_engine_npu.get_host_ub_devices')
async def test_npu_inventory_preserves_p_idev_relation(mock_get):
    mock_get.return_value = [FakeHostDevice('NPU', '1-1', 'vfe-guid', affinity_devs=[
        {'device_type': 'UBCTRL', 'device_id': '255-10-1'},
        {'device_type': 'NIC_PFE', 'device_id': '1-11-3'},
        {'device_type': 'NIC_VFE', 'device_id': '1-11-3-49'},
        {'device_type': 'P_IDEV', 'device_id': '00020'},
    ]), FakeHostDevice('P_IDEV', '00020', 'fe-guid')]
    devices = await NpuClient.get_host_ub_devices()
    assert len(devices) == 1
    assert devices[0].guid == 'vfe-guid'
    assert devices[0].affinity_devs[-1] == AffinityDeviceInfo(device_type='P_IDEV', device_id='00020')
