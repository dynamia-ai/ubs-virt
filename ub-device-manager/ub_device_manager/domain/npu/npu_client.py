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
import re
import secrets
import subprocess
from enum import IntEnum
from pathlib import Path
from typing import Any, List, Optional

from loguru import logger
from pydantic import BaseModel, Field, constr



from ub_device_manager.common.wrapper import try_catch_log


class AffinityDeviceInfo(BaseModel):
    device_id: str = Field(..., description='Affinity device id.', examples=['1-1'])
    device_type: str = Field(
        ...,
        description='Affinity device type.',
        examples=['NIC_VFE', 'P_IDEV'],
    )


class UbDeviceInfo(BaseModel):
    """Host UB device inventory entry, including NPUs and passthrough NICs."""
    id: constr(max_length=64) = Field(
        ..., description='NPU device ID (fixed-length string)', examples=['0-1']
    )
    type: constr(max_length=32) = Field(
        ..., description='NPU device type (fixed-length string)', examples=['NPU']
    )
    guid: constr(max_length=128) = Field(
        ...,
        description='NPU device GUID (fixed-length string)',
        examples=['0xcc08-a000-0-2-000000-00000000000201ff'],
    )
    bus_guid: Optional[constr(max_length=128)] = Field(
        None,
        description='NPU device BUS_INSTANCE_GUID (fixed-length string)',
        examples=['0xcc08-a002-0-0-000000-00000038c8c1008c'],
    )
    affinity_devs: List[AffinityDeviceInfo] = Field(
        default_factory=list,
        description='Devices that have an affinity relationship with the current UB device.',
    )


class DeviceRef(BaseModel):
    device_id: constr(min_length=1, max_length=64) = Field(
        ..., description='UB device ID', examples=['1-1']
    )
    device_type: constr(min_length=1, max_length=64) = Field(
        ..., description='UB device type', examples=['NPU']
    )


class AllocDevicesParams(BaseModel):
    """Input for the downstream component's device allocation interface."""
    upi: constr(min_length=1, max_length=64) = Field(
        ..., description='User isolation identifier', examples=['0x000f']
    )
    bus_guid: str = Field('', description='Existing bus instance GUID; pass an empty string to create a new bus instance.')
    device_list: List[DeviceRef] = Field(..., min_length=1, description='List of UB devices to allocate.')


# Device types allowed for VM passthrough.
UBSE_PASSTHROUGH_DEVICE_TYPES = {"NPU", "NIC_PFE", "NIC_VFE"}


class ProductType(IntEnum):
    SERVER = 0
    POD_16_1825 = 1
    POD_32_1825 = 2


NET_SYSFS_ROOT = Path("/sys/class/net")
HINICADM_PATH = "/usr/sbin/hinicadm5"
NIC_COMMAND_TIMEOUT = 30
SAFE_INTERFACE_NAME = re.compile(r"^[A-Za-z0-9_.:-]{1,15}$")
SAFE_FUNCTION_ID = re.compile(r"^[0-9]+$")
SAFE_LOCAL_MAC = re.compile(r"^02(?::[0-9a-fA-F]{2}){5}$")


_ubse_log_registered = False


def _register_ubse_log_callback() -> None:
    # The UBSE log callback is process-wide and must be registered once.
    global _ubse_log_registered
    if _ubse_log_registered:
        return
    from ubse.ubs_engine_log import ubs_engine_log_callback_register
    ubs_engine_log_callback_register(lambda level, msg: None)
    _ubse_log_registered = True


class NpuClient:
    def __init__(self):
        _register_ubse_log_callback()

    @staticmethod
    @try_catch_log
    async def get_host_ub_devices() -> list[UbDeviceInfo]:
        """Query and filter the host UB device inventory by the passthrough allowlist."""
        # Propagate UBSE failures after the decorator logs them; the API returns HTTP 500.
        from ubse.ubs_engine_npu import get_host_ub_devices
        host_devices = await asyncio.to_thread(get_host_ub_devices)
        return [UbDeviceInfo(
            type=dev.device_type,
            id=dev.device_id,
            guid=dev.guid,
            bus_guid=dev.bus_instance,
            affinity_devs=dev.affinity_devs)
            for dev in host_devices if dev.device_type in UBSE_PASSTHROUGH_DEVICE_TYPES]

    @staticmethod
    async def get_product_type() -> ProductType:
        """Read the local UBSE product type; unknown values fail closed."""
        from ubse import ubs_engine_npu

        get_type = getattr(ubs_engine_npu, "get_product_type", None)
        if get_type is None:
            raise RuntimeError("UBSE get_product_type is unavailable.")
        raw_type = await asyncio.to_thread(get_type)
        try:
            if type(raw_type) is not int:
                raise ValueError("UBSE product type must be an integer.")
            return ProductType(raw_type)
        except ValueError as exc:
            raise RuntimeError(f"Unknown UBSE product type: {raw_type!r}.") from exc

    @staticmethod
    def _find_interface(pfe_guid: str, net_root: Path = NET_SYSFS_ROOT) -> str:
        matches = []
        for entry in net_root.iterdir():
            if not SAFE_INTERFACE_NAME.fullmatch(entry.name):
                continue
            try:
                guid = (entry / "device" / "guid").read_text(encoding="utf-8").strip()
            except (OSError, UnicodeError):
                continue
            if guid.lower() == pfe_guid.strip().lower():
                matches.append(entry.name)
        if len(matches) != 1:
            raise RuntimeError(f"Expected one NIC interface for PFE {pfe_guid}, found {len(matches)}.")
        return matches[0]

    @staticmethod
    def _run_nic_command(interface: str, function_id: str, option: str, value: str | None = None) -> None:
        if not SAFE_INTERFACE_NAME.fullmatch(interface) or not SAFE_FUNCTION_ID.fullmatch(function_id):
            raise ValueError("Invalid NIC interface or VFE function ID.")
        if not ((option == "-m" and value is not None and SAFE_LOCAL_MAC.fullmatch(value)) or
                (option == "-c" and value is None)):
            raise ValueError("Invalid NIC vfconfig operation or value.")
        command = ["/usr/bin/sudo", "-n", HINICADM_PATH,
                   "vfconfig", "-i", interface, "-f", function_id, option]
        if value is not None:
            command.append(value)
        result = subprocess.run(
            command,
            check=True, capture_output=True, text=True, timeout=NIC_COMMAND_TIMEOUT,
        )
        if option == "-m":
            logger.info(
                "VF MAC configuration command succeeded: interface={}, function_id={}, mac={}, "
                "exit_code={}, stdout={!r}, stderr={!r}.",
                interface, function_id, value, result.returncode, result.stdout, result.stderr,
            )
        else:
            logger.info(
                "VF MAC/SVLAN clear command succeeded: interface={}, function_id={}, "
                "exit_code={}, stdout={!r}, stderr={!r}.",
                interface, function_id, result.returncode, result.stdout, result.stderr,
            )

    @classmethod
    async def configure_nics(cls, pairs: list[tuple[str, str]]) -> list[tuple[str, str]]:
        """Set a random locally administered MAC for each selected VF."""
        configured = []
        used_macs = set()
        pfe_guid = None
        function_id = None
        interface = None
        step = "resolve interface"
        try:
            for pfe_guid, function_id in pairs:
                interface = None
                step = "resolve interface"
                interface = await asyncio.to_thread(cls._find_interface, pfe_guid)
                step = "generate MAC"
                while True:
                    mac = "02:" + ":".join(f"{byte:02x}" for byte in secrets.token_bytes(5))
                    if mac not in used_macs:
                        used_macs.add(mac)
                        break
                step = "set MAC"
                await asyncio.to_thread(cls._run_nic_command, interface, function_id, "-m", mac)
                configured.append((interface, function_id))
            return configured
        except BaseException as exc:
            logger.error(
                "NIC configuration failed before rollback: step={}, pfe_guid={}, interface={}, "
                "function_id={}, error={}, exit_code={}, stdout={!r}, stderr={!r}.",
                step, pfe_guid, interface, function_id, exc,
                getattr(exc, "returncode", None),
                getattr(exc, "stdout", None), getattr(exc, "stderr", None),
            )
            await cls.clear_nic_configs(configured)
            raise

    @classmethod
    async def clear_nic_configs(cls, configured: list[tuple[str, str]]) -> None:
        """Clear VF MAC/SVLAN configuration written during a failed VM creation."""
        for interface, function_id in reversed(configured):
            try:
                await asyncio.to_thread(cls._run_nic_command, interface, function_id, "-c")
            except Exception as exc:
                logger.error(
                    "Failed to clear VF MAC/SVLAN configuration: interface={}, function_id={}, "
                    "error={}, exit_code={}, stdout={!r}, stderr={!r}.",
                    interface, function_id, exc, getattr(exc, "returncode", None),
                    getattr(exc, "stdout", None), getattr(exc, "stderr", None),
                )

    @try_catch_log
    async def bind_devices(self, params: AllocDevicesParams) -> tuple[str, int, int, int]:
        """Allocate devices and retrieve mapping data, releasing devices if retrieval fails."""
        from ubse.ubs_engine_npu import alloc_devices, free_devices, query_uba_tid_size
        device_list = [device.model_dump() for device in params.device_list]
        # UBSE accepts UPI values without the 0x prefix.
        ubse_upi = params.upi[2:] if params.upi.lower().startswith("0x") else params.upi
        bus_guid, _ = await asyncio.to_thread(
            alloc_devices,
            upi=ubse_upi,
            bus_guid=params.bus_guid,
            device_list=device_list,
        )
        try:
            tid, uba, size = await asyncio.to_thread(
                query_uba_tid_size,
                bus_instance_guid=bus_guid,
            )
            return bus_guid, tid, uba, size
        except Exception:
            # Release allocated devices when mapping retrieval fails.
            await asyncio.to_thread(
                free_devices,
                bus_instance_guid=bus_guid,
                device_list=device_list,
            )
            raise

    @staticmethod
    @try_catch_log
    async def free_devices(bus_instance_guid: str, device_list: list[dict[str, Any]]) -> None:
        from ubse.ubs_engine_npu import free_devices
        await asyncio.to_thread(
            free_devices,
            bus_instance_guid=bus_instance_guid,
            device_list=device_list,
        )

