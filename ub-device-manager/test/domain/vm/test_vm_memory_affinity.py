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
import pytest

from ub_device_manager.domain.vm.vm_memory_affinity import VmMemoryAffinityParser


def memory_xml(tune='<memory mode="strict" nodeset="1"/>', pages='', cells=None):
    cpu = '' if cells is None else f'<cpu><numa>{cells}</numa></cpu>'
    return (f'<domain>{cpu}<memoryBacking><hugepages>{pages}</hugepages></memoryBacking>'
            f'<numatune>{tune}</numatune></domain>')


def test_implicit_guest_cell_uses_host_binding():
    assert VmMemoryAffinityParser().parse(memory_xml()).cell_to_host_nodes == {0: {1}}


def test_guest_hugepage_nodes_are_not_host_nodes_and_cpu_pinning_is_ignored():
    xml = memory_xml('<memnode cellid="0" mode="strict" nodeset="3"/>',
                     '<page size="1" unit="G" nodeset="0"/>', '<cell id="0"/>')
    xml = xml.replace('<domain>', '<domain><cputune><vcpupin vcpu="0" cpuset="0"/></cputune>')
    assert VmMemoryAffinityParser().parse(xml).host_nodes == {3}


def test_per_cell_override_and_global_inheritance():
    xml = memory_xml('<memory mode="strict" nodeset="1"/>'
                     '<memnode cellid="1" mode="strict" nodeset="3"/>',
                     '<page size="1" nodeset="0-1"/>', '<cell id="0"/><cell id="1"/>')
    assert VmMemoryAffinityParser().parse(xml).cell_to_host_nodes == {0: {1}, 1: {3}}


def test_default_hugepages_cover_remaining_cells():
    xml = memory_xml(pages='<page size="1" unit="G"/><page size="2" unit="M" nodeset="1"/>',
                     cells='<cell id="0"/><cell id="1"/>')
    assert VmMemoryAffinityParser().parse(xml).host_nodes == {1}


@pytest.mark.parametrize('tune', [
    '', '<memory nodeset="1" mode="preferred"/>',
    '<memory nodeset="1" mode="interleave"/>',
    '<memory nodeset="1" mode="restrictive"/>',
    '<memory nodeset="1" placement="auto"/>',
    '<memory mode="strict"/>',
    '<memory nodeset="-1"/>', '<memory nodeset="1,,2"/>',
    '<memory nodeset="3-1"/>', '<memory nodeset="1,^1"/>',
    '<memory nodeset="999999999"/>',
    '<memnode cellid="9" nodeset="1"/>',
    '<memnode cellid="0" nodeset="1"/><memnode cellid="0" nodeset="3"/>',
])
def test_ambiguous_or_invalid_memory_binding_is_rejected(tune):
    with pytest.raises(ValueError):
        VmMemoryAffinityParser().parse(memory_xml(tune))


def test_hugepages_are_required():
    with pytest.raises(ValueError, match='hugepage'):
        VmMemoryAffinityParser().parse('<domain><numatune><memory nodeset="1"/></numatune></domain>')


@pytest.mark.parametrize('pages', [
    '<page size="1" nodeset="0"/>',
    '<page size="1" nodeset="0-2"/>',
    '<page size="1" nodeset="0-1"/><page size="2" nodeset="1"/>',
    '<page size="0"/>', '<page size="1"/><page size="2"/>',
])
def test_partial_or_conflicting_hugepage_coverage_is_rejected(pages):
    with pytest.raises(ValueError):
        VmMemoryAffinityParser().parse(memory_xml(pages=pages, cells='<cell id="0"/><cell id="1"/>'))


def test_duplicate_guest_cells_are_rejected():
    with pytest.raises(ValueError, match='duplicate'):
        VmMemoryAffinityParser().parse(memory_xml(cells='<cell id="0"/><cell id="0"/>'))


def test_additional_memory_devices_are_rejected():
    xml = memory_xml().replace('</domain>', '<devices><memory model="dimm"/></devices></domain>')
    with pytest.raises(ValueError, match='additional memory'):
        VmMemoryAffinityParser().parse(xml)


def test_namespaced_xml_and_exclusions():
    xml = memory_xml('<memory nodeset="0-3,^0,^2-3"/>').replace('<domain>', '<domain xmlns="urn:test">')
    assert VmMemoryAffinityParser().parse(xml).host_nodes == {1}


@pytest.mark.parametrize(('nodeset', 'expected'), [
    ('1,2', {1, 2}), ('1-3', {1, 2, 3}), ('0-3,^0,^3', {1, 2}),
])
def test_one_guest_cell_can_bind_to_multiple_host_nodes(nodeset, expected):
    xml = memory_xml(f'<memory mode="strict" placement="static" nodeset="{nodeset}"/>')
    affinity = VmMemoryAffinityParser().parse(xml)
    assert affinity.cell_to_host_nodes == {0: expected}
    assert affinity.host_nodes == expected


def test_per_cell_multiple_nodes_override_global_binding():
    xml = memory_xml('<memory mode="strict" nodeset="1,2"/>'
                     '<memnode cellid="1" mode="strict" nodeset="2,3"/>',
                     '<page size="2" unit="MiB" nodeset="0-1"/>', '<cell id="0"/><cell id="1"/>')
    affinity = VmMemoryAffinityParser().parse(xml)
    assert affinity.cell_to_host_nodes == {0: {1, 2}, 1: {2, 3}}
    assert affinity.host_nodes == {1, 2, 3}


@pytest.mark.parametrize('mode', ['preferred', 'interleave', 'restrictive'])
def test_multiple_host_nodes_still_require_strict_memory_binding(mode):
    with pytest.raises(ValueError, match='strict host NUMA binding'):
        VmMemoryAffinityParser().parse(memory_xml(f'<memory mode="{mode}" nodeset="1,2"/>'))
