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
from enum import StrEnum
from typing import Protocol, Sequence, TypeVar

from ub_device_manager.domain.npu.host_topology import HostTopology


class NpuAffinityPolicy(StrEnum):
    REQUIRED = "required"
    SOCKET = "socket"
    PREFERRED = "preferred"


class _Device(Protocol):
    id: str


DeviceT = TypeVar("DeviceT", bound=_Device)


class NpuAffinitySelector:
    def filter_and_rank(self, candidates: Sequence[DeviceT], policy: NpuAffinityPolicy | str,
                        memory_nodes: set[int], topology: HostTopology) -> list[DeviceT]:
        policy = NpuAffinityPolicy(policy)
        if not memory_nodes or not memory_nodes <= topology.numa_to_socket.keys():
            raise ValueError("memory binding references an unknown host NUMA node")
        sockets = {topology.numa_to_socket[node] for node in memory_nodes}
        if policy == NpuAffinityPolicy.SOCKET and None in sockets:
            raise ValueError("socket topology is unknown for a memory NUMA node")
        sockets.discard(None)

        def tier(device: DeviceT) -> int:
            node = topology.npu_to_numa.get(device.id)
            if policy == NpuAffinityPolicy.REQUIRED:
                if node in memory_nodes:
                    return 0
                return 1
            if topology.numa_to_socket.get(node) in sockets:
                return 0
            return 1

        limit = 1 if policy == NpuAffinityPolicy.PREFERRED else 0
        ranked = []
        for device in candidates:
            priority = tier(device)
            if priority <= limit:
                ranked.append((priority, device))
        ranked.sort(key=lambda item: (item[0], item[1].id))
        return [device for _, device in ranked]

    def select(self, candidates: Sequence[DeviceT], count: int, policy: NpuAffinityPolicy | str,
               memory_nodes: set[int], topology: HostTopology) -> list[DeviceT]:
        ranked = self.filter_and_rank(candidates, policy, memory_nodes, topology)
        if len(ranked) < count:
            raise ValueError(f"not enough free NPU devices for affinity_policy={policy}: "
                             f"requested={count}, eligible={len(ranked)}")
        return ranked[:count]
