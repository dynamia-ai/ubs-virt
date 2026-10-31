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
import re
from xml.etree.ElementTree import Element

from defusedxml import ElementTree


def _children(node: Element, name: str) -> list[Element]:
    return [child for child in node if child.tag.rsplit("}", 1)[-1] == name]


def _child(node: Element, name: str) -> Element | None:
    matches = _children(node, name)
    if len(matches) > 1:
        raise ValueError(f"duplicate {name} element")
    return matches[0] if matches else None


@dataclass(frozen=True)
class VmMemoryAffinity:
    cell_to_host_nodes: dict[int, set[int]]

    @property
    def host_nodes(self) -> set[int]:
        nodes = set()
        for bound_nodes in self.cell_to_host_nodes.values():
            nodes.update(bound_nodes)
        return nodes


class VmMemoryAffinityParser:
    def parse(self, xml: str) -> VmMemoryAffinity:
        root = ElementTree.fromstring(xml)
        backing = _child(root, "memoryBacking")
        hugepages = _child(backing, "hugepages") if backing is not None else None
        if hugepages is None:
            raise ValueError("affinity_policy requires hugepage-backed memory")
        devices = _child(root, "devices")
        if devices is not None and _children(devices, "memory"):
            raise ValueError("affinity_policy does not support additional memory devices")

        cpu = _child(root, "cpu")
        numa = _child(cpu, "numa") if cpu is not None else None
        cells = _children(numa, "cell") if numa is not None else []
        cell_ids: set[int] = set()
        for cell in cells:
            cell_id = self._parse_id(cell.get("id"), "guest NUMA cell id")
            if cell_id in cell_ids:
                raise ValueError("duplicate guest NUMA cell id")
            cell_ids.add(cell_id)
        if numa is not None and not cells:
            raise ValueError("guest NUMA topology has no cells")
        if not cell_ids:
            cell_ids = {0}
        self._validate_hugepage_coverage(hugepages, cell_ids)

        numatune = _child(root, "numatune")
        if numatune is None:
            raise ValueError("affinity_policy requires explicit host memory NUMA binding")
        memory = _child(numatune, "memory")
        if memory is not None and memory.get("placement", "static") != "static":
            raise ValueError("automatic memory placement is not supported")
        memnodes: dict[int, Element] = {}
        for node in _children(numatune, "memnode"):
            cell_id = self._parse_id(node.get("cellid"), "memnode cellid")
            if cell_id not in cell_ids or cell_id in memnodes:
                raise ValueError("unknown or duplicate memnode cellid")
            memnodes[cell_id] = node

        result = {}
        for cell_id in sorted(cell_ids):
            node = memnodes.get(cell_id)
            if node is None:
                node = memory
            if node is None or node.get("mode", "strict") != "strict":
                raise ValueError("every guest memory cell requires strict host NUMA binding")
            nodeset = node.get("nodeset")
            if nodeset is None or not nodeset.strip():
                raise ValueError(f"memory binding nodeset is missing or empty for guest NUMA cell {cell_id}")
            result[cell_id] = self._parse_nodeset(nodeset)
        return VmMemoryAffinity(result)

    @staticmethod
    def _parse_id(value: str | None, label: str) -> int:
        if value is None or not re.fullmatch(r"[0-9]+", value) or int(value) > 65535:
            raise ValueError(f"invalid {label}")
        return int(value)

    @classmethod
    def _parse_nodeset(cls, value: str) -> set[int]:
        included: set[int] = set()
        excluded: set[int] = set()
        for item in value.split(","):
            match = re.fullmatch(r"(\^?)([0-9]+)(?:-([0-9]+))?", item.strip())
            if match is None:
                raise ValueError("invalid NUMA nodeset")
            start = cls._parse_id(match[2], "NUMA node")
            end = cls._parse_id(match[3] or match[2], "NUMA node")
            if end < start:
                raise ValueError("invalid NUMA nodeset range")
            target = excluded if match[1] else included
            target.update(range(start, end + 1))
        nodes = included - excluded
        if not nodes:
            raise ValueError("empty NUMA nodeset")
        return nodes

    def _validate_hugepage_coverage(self, hugepages: Element, cell_ids: set[int]) -> None:
        pages = _children(hugepages, "page")
        if not pages:
            return
        covered: set[int] = set()
        has_default = False
        for page in pages:
            size = page.get("size", "")
            if not re.fullmatch(r"[0-9]+", size) or int(size) <= 0:
                raise ValueError("invalid hugepage size")
            if page.get("nodeset") is None:
                if has_default:
                    raise ValueError("duplicate default hugepage configuration")
                has_default = True
                continue
            nodes = self._parse_nodeset(page.get("nodeset", ""))
            if not nodes <= cell_ids or covered & nodes:
                raise ValueError("unknown or duplicate guest hugepage nodes")
            covered.update(nodes)
        if not has_default and covered != cell_ids:
            raise ValueError("hugepages must cover every guest memory cell")
