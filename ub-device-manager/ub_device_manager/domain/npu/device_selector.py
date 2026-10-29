from __future__ import annotations

from enum import StrEnum
from typing import Optional, cast

from pydantic import BaseModel, constr

from ub_device_manager.domain.npu.npu_client import UbDeviceInfo


class UbDevice(BaseModel):
    type: str
    id: str
    guid: Optional[constr(max_length=128)] = None


class UbDeviceType(StrEnum):
    NPU = "NPU"
    NIC_PFE = "NIC_PFE"
    NIC_VFE = "NIC_VFE"


DEFAULT_NIC_TYPE = UbDeviceType.NIC_VFE
DeviceKey = tuple[str, str]
InventoryIndex = dict[DeviceKey, UbDeviceInfo]


class UbDeviceSelector:
    def resolve_npu_devices(
        self,
        inventory: list[UbDeviceInfo],
        ids: Optional[list[str]] = None,
        count: Optional[int] = None,
        need_nic: bool = False,
    ) -> list[UbDevice]:
        """Select free NPUs by ID or count and optionally append one affinity NIC per NPU."""
        # Build an index for the full UB device inventory.
        inventory_index: InventoryIndex = {(device.type, device.id): device for device in inventory}
        if ids:
            selected = self._resolve_explicit_npus(ids, inventory_index)
        else:
            # Request validation guarantees that count is valid here; cast only narrows the type.
            selected = self._select_free_npus(cast(int, count), inventory_index, need_nic)
        if need_nic:
            selected = self._append_affinity_nics(selected, inventory_index)
        return selected

    def _resolve_explicit_npus(self, ids: list[str], inventory_index: InventoryIndex) -> list[UbDevice]:
        """Resolve explicitly requested NPUs that exist and are not bound to a bus instance."""
        selected: list[UbDevice] = []
        for device_id in ids:
            device = inventory_index.get((UbDeviceType.NPU, device_id))
            if not device:
                raise ValueError(f"UB device not found, type: {UbDeviceType.NPU}, id: {device_id}")
            # A device with a bus instance GUID is already bound and cannot be bound again.
            if device.bus_guid:
                raise ValueError(f"UB device is already bound, type: {device.type}, id: {device.id}")
            selected.append(UbDevice(type=device.type, id=device.id, guid=device.guid))
        return selected

    def _select_free_npus(self, count: int, inventory_index: InventoryIndex, need_nic: bool) -> list[UbDevice]:
        """Select the first count free NPUs when sufficient devices are available."""
        candidates: list[UbDeviceInfo] = []
        for device in inventory_index.values():
            if device.type != UbDeviceType.NPU or device.bus_guid:
                continue
            if need_nic and not self._find_affinity_device(device, inventory_index, DEFAULT_NIC_TYPE):
                continue
            candidates.append(device)

        if len(candidates) < count:
            if need_nic:
                raise ValueError(
                    f"not enough free {UbDeviceType.NPU} devices with available affinity {DEFAULT_NIC_TYPE}"
                )
            raise ValueError(f"not enough free {UbDeviceType.NPU} devices")
        return [
            UbDevice(type=device.type, id=device.id, guid=device.guid)
            for device in candidates[:count]
        ]

    def _append_affinity_nics(self, devices: list[UbDevice], inventory_index: InventoryIndex) -> list[UbDevice]:
        """Append one affinity NIC-VFE for each selected NPU."""
        selected = list(devices)
        for device in devices:
            npu = inventory_index[(device.type, device.id)]
            nic = self._find_affinity_device(npu, inventory_index, DEFAULT_NIC_TYPE)
            if nic is None:
                raise ValueError(f"NPU {device.id} has no available affinity {DEFAULT_NIC_TYPE}")
            selected.append(UbDevice(type=nic.type, id=nic.id, guid=nic.guid))
        return selected

    def _find_affinity_device(
        self, device: UbDeviceInfo, inventory_index: InventoryIndex, nic_type: str
    ) -> Optional[UbDeviceInfo]:
        """Find an available affinity NIC-VFE."""
        for affinity_dev in device.affinity_devs:
            # The affinity list includes NIC-PFE and NIC-VFE; only NIC-VFE supports passthrough.
            if affinity_dev.device_type != nic_type:
                continue
            nic = inventory_index.get((affinity_dev.device_type, affinity_dev.device_id))
            # The NIC-VFE must exist and be unbound.
            if nic and not nic.bus_guid:
                return nic
        return None
