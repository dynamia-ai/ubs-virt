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

from test.domain.npu.test_device_selector import make_device
from ub_device_manager.domain.npu.device_selector import UbDeviceSelector
from ub_device_manager.domain.npu.host_topology import HostTopology


TOPOLOGY = HostTopology({f'npu{i}': 3 if i < 4 else 1 for i in range(8)},
                        {0: 0, 1: 0, 2: 1, 3: 1})


def select(nodes, policy, count=1, inventory=None, **kwargs):
    return UbDeviceSelector().resolve_npu_devices(
        [make_device(f'npu{i}') for i in range(8)] if inventory is None else inventory,
        count=count, affinity_policy=policy, memory_nodes=set(nodes), topology=TOPOLOGY, **kwargs)


@pytest.mark.parametrize(('nodes', 'policy', 'expected'), [
    ({0}, 'socket', ['npu4']), ({2}, 'socket', ['npu0']),
    ({1}, 'required', ['npu4']), ({3}, 'required', ['npu0']),
    ({0, 2}, 'socket', ['npu0']), ({1, 3}, 'required', ['npu0']),
])
def test_hard_policy_topology_examples(nodes, policy, expected):
    assert [d.id for d in select(nodes, policy)] == expected


@pytest.mark.parametrize(('nodes', 'policy', 'count'), [
    ({0}, 'required', 1), ({2}, 'required', 1),
    ({0, 2}, 'required', 2), ({1}, 'required', 8), ({1}, 'socket', 8),
    ({1}, 'preferred', 9),
])
def test_hard_constraints_and_total_shortage_fail(nodes, policy, count):
    with pytest.raises(ValueError, match='not enough'):
        select(nodes, policy, count)


def test_preferred_keeps_same_socket_devices_and_fills_from_remote():
    assert [d.id for d in select({1}, 'preferred', 8)] == [
        'npu4', 'npu5', 'npu6', 'npu7', 'npu0', 'npu1', 'npu2', 'npu3']


def test_preferred_ranks_same_socket_before_other_socket_and_unknown():
    assert [d.id for d in select({0}, 'preferred', 4,
                                [make_device('unknown'), make_device('npu0'),
                                 make_device('npu5'), make_device('npu4')])] == [
        'npu4', 'npu5', 'npu0', 'unknown']


def test_bound_devices_are_never_fallback_candidates():
    with pytest.raises(ValueError, match='not enough'):
        select({1}, 'preferred', inventory=[make_device('npu4', bus_guid='busy')])


def test_unknown_topology_only_satisfies_preferred():
    assert select({1}, 'preferred', inventory=[make_device('unknown')])[0].id == 'unknown'
    for policy in ('required', 'socket'):
        with pytest.raises(ValueError):
            select({1}, policy, inventory=[make_device('unknown')])


def test_nic_requirement_is_applied_before_affinity_selection():
    devices = [make_device('npu4'), make_device('npu5', affinity=[('nic', 'NIC_VFE')]),
               make_device('nic', device_type='NIC_VFE')]
    assert [d.id for d in select({1}, 'required', inventory=devices, need_nic=True)] == ['npu5', 'nic']


def test_omitted_policy_preserves_original_inventory_order():
    devices = [make_device('npu7'), make_device('npu0')]
    assert UbDeviceSelector().resolve_npu_devices(devices, count=1)[0].id == 'npu7'


def test_unknown_memory_node_and_missing_socket_fail():
    with pytest.raises(ValueError, match='unknown host NUMA'):
        select({9}, 'preferred')
    selector = UbDeviceSelector()
    with pytest.raises(ValueError, match='socket topology'):
        selector.resolve_npu_devices([make_device('npu4')], count=1, affinity_policy='socket',
                                     memory_nodes={1}, topology=HostTopology({'npu4': 1}, {1: None}))


@pytest.mark.parametrize(('policy', 'count', 'expected'), [
    ('required', 1, ['z-local']),
    ('socket', 2, ['a-same-socket', 'z-local']),
    ('preferred', 4, ['a-same-socket', 'z-local', '0-unknown', 'b-remote']),
])
def test_policy_pools_do_not_add_numalocal_priority(policy, count, expected):
    topology = HostTopology({'z-local': 1, 'a-same-socket': 0, 'b-remote': 3},
                            {0: 0, 1: 0, 3: 1})
    inventory = [make_device(device_id) for device_id in [
        'z-local', '0-unknown', 'b-remote', 'a-same-socket'
    ]]
    selected = UbDeviceSelector().resolve_npu_devices(
        inventory, count=count, affinity_policy=policy, memory_nodes={1}, topology=topology
    )
    assert [device.id for device in selected] == expected
