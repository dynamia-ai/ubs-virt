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
from dataclasses import dataclass
from pathlib import Path
from typing import TYPE_CHECKING

from loguru import logger

from ub_device_manager.exceptions import AffinityTopologyUnavailable

if TYPE_CHECKING:
    from ub_device_manager.domain.npu.npu_client import UbDeviceInfo


@dataclass(frozen=True)
class HostTopology:
    npu_to_numa: dict[str, int]
    numa_to_socket: dict[int, int | None]


class HostTopologyProvider:
    def __init__(self, sysfs_root: Path | None = None, ub_devices_root: Path | None = None):
        self.sysfs_root = Path("/sys/devices/system") if sysfs_root is None else Path(sysfs_root)
        self.ub_devices_root = Path("/sys/bus/ub/devices") if ub_devices_root is None else Path(ub_devices_root)

    def get_topology(self, inventory: list['UbDeviceInfo']) -> HostTopology:
        try:
            numa_to_socket = self._read_numa_to_socket()
            npu_to_numa = {}
            for device in inventory:
                if device.type != "NPU":
                    continue
                fe_devices = [item for item in device.affinity_devs if item.device_type == "P_IDEV"]
                if len(fe_devices) != 1:
                    raise ValueError(f"NPU {device.id} requires exactly one P_IDEV affinity device")
                fe_id = fe_devices[0].device_id
                if not fe_id or fe_id in {".", ".."} or "/" in fe_id or "\\" in fe_id or "\x00" in fe_id:
                    raise ValueError(f"NPU {device.id} has an invalid UB FE ID: {fe_id!r}")
                numa_path = self.ub_devices_root / fe_id / "numa"
                try:
                    numa_text = numa_path.read_text(encoding="utf-8").strip()
                    node = int(numa_text, 16)
                    logger.debug("NPU NUMA mapping: npu_id={}, fe_id={}, raw_value={!r}, numa={}",
                                 device.id, fe_id, numa_text, node)
                except (OSError, ValueError) as exc:
                    raise AffinityTopologyUnavailable(
                        f"Cannot read NUMA for NPU {device.id}, UB FE {fe_id}, path {numa_path}: {exc}"
                    ) from exc
                if node < 0 or node not in numa_to_socket:
                    raise ValueError(f"NPU {device.id}, UB FE {fe_id} references an unknown host NUMA node: {node}")
                npu_to_numa[device.id] = node
            return HostTopology(npu_to_numa, numa_to_socket)
        except (OSError, ValueError) as exc:
            raise AffinityTopologyUnavailable(str(exc)) from exc

    def _read_numa_to_socket(self) -> dict[int, int | None]:
        node_sockets: dict[int, set[int | None]] = {}
        for node in (self.sysfs_root / "node").glob("node[0-9]*"):
            if node.name[4:].isdigit():
                node_sockets[int(node.name[4:])] = set()
        for cpu in (self.sysfs_root / "cpu").glob("cpu[0-9]*"):
            if not cpu.name[3:].isdigit():
                continue
            nodes = [int(node.name[4:]) for node in cpu.glob("node[0-9]*") if node.name[4:].isdigit()]
            if not nodes:
                continue
            if len(nodes) != 1:
                raise ValueError(f"ambiguous NUMA membership for {cpu.name}")
            node = nodes[0]
            try:
                value = int((cpu / "topology" / "physical_package_id").read_text().strip())
                socket = value if value >= 0 else None
            except (FileNotFoundError, ValueError):
                socket = None
            node_sockets.setdefault(node, set()).add(socket)
        if not node_sockets:
            raise ValueError("host NUMA-to-Socket topology is unavailable in sysfs")
        return {node: next(iter(sockets)) if len(sockets) == 1 else None
                for node, sockets in node_sockets.items()}
