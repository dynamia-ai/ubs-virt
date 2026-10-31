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
from pathlib import Path
from unittest.mock import AsyncMock, patch

import pytest

from ub_device_manager.domain.npu.host_topology import HostTopologyProvider
from ub_device_manager.domain.npu.npu_client import UbDeviceInfo
from ub_device_manager.exceptions import AffinityTopologyUnavailable


def add_cpu(root, cpu_id, node, socket):
    cpu = root / 'cpu' / f'cpu{cpu_id}'
    (cpu / f'node{node}').mkdir(parents=True)
    (root / 'node' / f'node{node}').mkdir(parents=True, exist_ok=True)
    (cpu / 'topology').mkdir()
    if socket is not None:
        (cpu / 'topology' / 'physical_package_id').write_text(str(socket))


def add_fe(root, fe_id, node):
    directory = root / fe_id
    directory.mkdir(parents=True, exist_ok=True)
    value = f'0x{node:02x}' if isinstance(node, int) else node
    (directory / 'numa').write_text(value, encoding='utf-8')


def npu(device_id='1-1', fe_id='00020', bus_guid=None):
    return UbDeviceInfo(
        id=device_id, type='NPU', guid=f'vfe-guid-{device_id}', bus_guid=bus_guid,
        affinity_devs=[
            {'device_type': 'UBCTRL', 'device_id': '255-10-1'},
            {'device_type': 'NIC_PFE', 'device_id': '1-11-3'},
            {'device_type': 'NIC_VFE', 'device_id': '1-11-3-49'},
            {'device_type': 'P_IDEV', 'device_id': fe_id},
        ],
    )


@pytest.fixture
def environment(tmp_path):
    system = tmp_path / 'system'
    devices = tmp_path / 'ub' / 'devices'
    for cpu, node, socket in [(0, 0, 0), (96, 1, 0), (192, 2, 1), (288, 3, 1)]:
        add_cpu(system, cpu, node, socket)
    devices.mkdir(parents=True)
    return system, devices


def test_a5_fe_affinity_does_not_execute_commands(environment):
    system, devices = environment
    inventory = []
    fe_ids = ['0000a', '0000b', '0000c', '0000d', '00005', '00006', '00007', '00008']
    for index, fe_id in enumerate(fe_ids):
        inventory.append(npu(f'1-{index + 1}', fe_id))
        add_fe(devices, fe_id, 3 if index < 4 else 1)
    with patch('subprocess.run', side_effect=AssertionError('topology must only read sysfs')):
        topology = HostTopologyProvider(system, devices).get_topology(inventory)
    assert topology.npu_to_numa == {f'1-{i + 1}': 3 if i < 4 else 1 for i in range(8)}
    assert topology.numa_to_socket == {0: 0, 1: 0, 2: 1, 3: 1}


def test_fe_id_preserves_leading_zeros_and_is_not_npu_id_or_guid(environment):
    system, devices = environment
    add_fe(devices, '00020', '0x01\n')
    topology = HostTopologyProvider(system, devices).get_topology([npu()])
    assert topology.npu_to_numa == {'1-1': 1}


def test_fe_identity_supports_multiple_ubse_slots(environment):
    system, devices = environment
    add_fe(devices, '00020', 3)
    add_fe(devices, '00030', 1)
    topology = HostTopologyProvider(system, devices).get_topology([
        npu('1-1', '00020'), npu('2-1', '00030')
    ])
    assert topology.npu_to_numa == {'1-1': 3, '2-1': 1}


def test_non_npu_devices_are_not_used_as_topology_sources(environment):
    system, devices = environment
    add_fe(devices, '00020', 1)
    inventory = [npu(), UbDeviceInfo(type='NIC_VFE', id='nic', guid='nic-guid'),
                 UbDeviceInfo(type='NIC_PFE', id='pfe', guid='pfe-guid')]
    assert HostTopologyProvider(system, devices).get_topology(inventory).npu_to_numa == {'1-1': 1}


def test_empty_inventory_returns_no_npu_mapping(environment):
    system, devices = environment
    assert HostTopologyProvider(system, devices).get_topology([]).npu_to_numa == {}


@pytest.mark.parametrize('fe_ids', [[], ['00020', '00030']])
def test_missing_or_duplicate_fe_relation_is_rejected(environment, fe_ids):
    system, devices = environment
    data = npu().model_dump()
    data['affinity_devs'] = [
        {'device_type': 'P_IDEV', 'device_id': value} for value in fe_ids
    ]
    device = UbDeviceInfo.model_validate(data)
    with pytest.raises(AffinityTopologyUnavailable, match='exactly one P_IDEV'):
        HostTopologyProvider(system, devices).get_topology([device])


@pytest.mark.parametrize('fe_id', ['', '.', '..', '../00020', '/00020', 'a/b', 'a\\b', 'a\x00b'])
def test_fe_id_cannot_escape_device_directory(environment, fe_id):
    system, devices = environment
    with pytest.raises(AffinityTopologyUnavailable, match='invalid UB FE ID'):
        HostTopologyProvider(system, devices).get_topology([npu(fe_id=fe_id)])


@pytest.mark.parametrize('value', ['', 'invalid', '1,3', '-1', '99'])
def test_invalid_or_unknown_fe_numa_is_rejected(environment, value):
    system, devices = environment
    add_fe(devices, '00020', value)
    with pytest.raises(AffinityTopologyUnavailable):
        HostTopologyProvider(system, devices).get_topology([npu()])


def test_missing_fe_numa_reports_device_and_path(environment):
    system, devices = environment
    with pytest.raises(AffinityTopologyUnavailable, match='NPU 1-1, UB FE 00020') as error:
        HostTopologyProvider(system, devices).get_topology([npu()])
    assert str(devices / '00020' / 'numa') in error.value.detail


def test_fe_numa_permission_failure_is_not_hidden(environment):
    system, devices = environment
    provider = HostTopologyProvider(system, devices)
    with patch.object(provider, '_read_numa_to_socket', return_value={1: 0}), \
         patch.object(Path, 'read_text', side_effect=PermissionError('denied')):
        with pytest.raises(AffinityTopologyUnavailable, match='denied'):
            provider.get_topology([npu()])


def test_numa_to_socket_uses_sysfs_instead_of_number_arithmetic(tmp_path):
    add_cpu(tmp_path, 10, 5, 7)
    add_cpu(tmp_path, 19, 9, 11)
    assert HostTopologyProvider(tmp_path)._read_numa_to_socket() == {5: 7, 9: 11}


def test_unknown_socket_is_not_assumed_to_be_socket_zero(tmp_path):
    add_cpu(tmp_path, 0, 1, 0)
    add_cpu(tmp_path, 1, 1, 1)
    add_cpu(tmp_path, 2, 2, None)
    (tmp_path / 'node' / 'node8').mkdir()
    assert HostTopologyProvider(tmp_path)._read_numa_to_socket() == {1: None, 2: None, 8: None}


def test_missing_host_numa_topology_is_an_explicit_error(tmp_path):
    with pytest.raises(AffinityTopologyUnavailable, match='unavailable'):
        HostTopologyProvider(tmp_path).get_topology([npu()])


def test_topology_remains_available_across_bind_and_release(environment):
    system, devices = environment
    add_fe(devices, '00020', 1)
    provider = HostTopologyProvider(system, devices)
    device = npu()
    first = provider.get_topology([device])
    device.bus_guid = 'allocated-bus'
    device.guid = 'new-vfe-guid'
    bound = provider.get_topology([device])
    device.bus_guid = ''
    released = provider.get_topology([device])
    assert first == bound == released
    assert released.npu_to_numa == {'1-1': 1}


async def test_real_ubse_inventory_flows_through_fe_topology_and_binding(environment):
    from test.domain.npu.test_npu_client import FakeHostDevice
    from test.helpers import run_task
    from ub_device_manager.app.models import CreateVmRequest
    from ub_device_manager.constants import CREATE_VM_REQUEST_CONTEXT_KEY
    from ub_device_manager.domain.npu.npu_client import NpuClient
    from ub_device_manager.domain.npu.npu_tasks import BindNpuDeviceTask
    from ub_device_manager.domain.vm.vm_tasks import PrepareVmXmlTask

    system, devices = environment
    add_fe(devices, '00020', 3)
    add_fe(devices, '00021', 1)
    source = [npu('1-1', '00020'), npu('1-5', '00021')]
    inventory = [FakeHostDevice('NPU', d.id, d.guid,
                 affinity_devs=[item.model_dump() for item in d.affinity_devs]) for d in source]
    xml = ('<domain><memoryBacking><hugepages/></memoryBacking>'
           '<numatune><memory mode="strict" nodeset="1"/></numatune></domain>')
    request = CreateVmRequest(upi='0x1', xml_text=xml, npu_count=1, affinity_policy='required')
    provider = HostTopologyProvider(system, devices)
    with patch('ubse.ubs_engine_npu.get_host_ub_devices', return_value=inventory), \
         patch('ub_device_manager.domain.npu.npu_tasks.HostTopologyProvider', return_value=provider), \
         patch.object(NpuClient, 'bind_devices', new_callable=AsyncMock, return_value=('bus-1', 1, 2, 3)) as bind:
        context = await run_task(PrepareVmXmlTask(), {CREATE_VM_REQUEST_CONTEXT_KEY: request})
        await run_task(BindNpuDeviceTask(), context.to_dict())
    assert [(item.device_type, item.device_id) for item in bind.call_args.args[0].device_list] == [('NPU', '1-5')]


async def test_invalid_fe_prevents_allocation(environment):
    from test.helpers import run_task
    from ub_device_manager.app.models import CreateVmRequest
    from ub_device_manager.constants import CREATE_VM_REQUEST_CONTEXT_KEY
    from ub_device_manager.domain.npu.npu_client import NpuClient
    from ub_device_manager.domain.npu.npu_tasks import BindNpuDeviceTask
    from ub_device_manager.domain.vm.vm_tasks import PrepareVmXmlTask

    system, devices = environment
    request = CreateVmRequest(upi='0x1', npu_count=1, affinity_policy='required',
        xml_text='<domain><memoryBacking><hugepages/></memoryBacking>'
                 '<numatune><memory mode="strict" nodeset="1"/></numatune></domain>')
    with patch('ub_device_manager.domain.npu.npu_tasks.HostTopologyProvider',
               return_value=HostTopologyProvider(system, devices)), \
         patch.object(NpuClient, 'get_host_ub_devices', new_callable=AsyncMock, return_value=[npu()]), \
         patch.object(NpuClient, 'bind_devices', new_callable=AsyncMock) as bind:
        context = await run_task(PrepareVmXmlTask(), {CREATE_VM_REQUEST_CONTEXT_KEY: request})
        with pytest.raises(AffinityTopologyUnavailable):
            await run_task(BindNpuDeviceTask(), context.to_dict())
    bind.assert_not_awaited()


def test_http_creation_uses_fe_numa_but_passes_through_vfe_guids(environment):
    from fastapi.testclient import TestClient
    from test.test_end_to_end import FakeConnection, FakeDomain, FakeUbseDevice
    from test.domain.vm.test_vm_memory_affinity import memory_xml
    from ub_device_manager.common.factory import create_app
    from ub_device_manager.domain.vm.vm_smbios import SmbiosType131

    system, devices = environment
    add_fe(devices, '00020', 1)
    provider = HostTopologyProvider(system, devices)
    npu_guid = 'cc08-a002-0-2-000000-000000e70e940189'
    nic_guid = 'cc08-a004-0-4-000000-000000e70e940190'
    inventory = [FakeUbseDevice('NPU', '1-1', npu_guid, affinity_devs=[
        {'device_type': 'UBCTRL', 'device_id': '255-10-1'},
        {'device_type': 'NIC_PFE', 'device_id': '1-11-3'},
        {'device_type': 'NIC_VFE', 'device_id': '1-11-3-49'},
        {'device_type': 'P_IDEV', 'device_id': '00020'},
    ]), FakeUbseDevice('NIC_VFE', '1-11-3-49', nic_guid)]
    conn = FakeConnection(define_domain=FakeDomain())
    xml = memory_xml().replace('<domain>', '<domain><os><type>hvm</type></os>')
    with patch('libvirt.open', return_value=conn), \
         patch('ubse.ubs_engine_npu.get_host_ub_devices', return_value=inventory), \
         patch('ubse.ubs_engine_npu.alloc_devices', return_value=('bus-1', None)) as allocate, \
         patch('ubse.ubs_engine_npu.query_uba_tid_size', return_value=(1, 2, 3)), \
         patch('ub_device_manager.domain.npu.npu_tasks.HostTopologyProvider', return_value=provider), \
         patch('ub_device_manager.domain.vm.vm_smbios.VmSmbios.get_type131',
               return_value=SmbiosType131(0, 65535, 255, 1, 16383, 3)):
        with TestClient(create_app(), raise_server_exceptions=False) as client:
            response = client.post('/vm/create', json={
                'upi': '0x1', 'xml_text': xml, 'npu_count': 1,
                'need_nic': True, 'affinity_policy': 'required',
            })
    assert response.status_code == 200, response.text
    assert allocate.call_args.kwargs['device_list'] == [
        {'device_id': '1-1', 'device_type': 'NPU'},
        {'device_id': '1-11-3-49', 'device_type': 'NIC_VFE'},
    ]
    assert npu_guid in conn.defined_xml
    assert nic_guid in conn.defined_xml
    assert 'P_IDEV' not in conn.defined_xml


def test_hexadecimal_numa_is_converted_to_host_node_number(environment):
    system, devices = environment
    add_cpu(system, 512, 16, 5)
    add_fe(devices, '0000a', '0x10\n')
    topology = HostTopologyProvider(system, devices).get_topology([npu(fe_id='0000a')])
    assert topology.npu_to_numa == {'1-1': 16}
    assert topology.numa_to_socket[16] == 5


@pytest.mark.parametrize(('nodes', 'policy', 'expected'), [
    ({1}, 'required', ['1-5', '1-6', '1-7', '1-8']),
    ({3}, 'required', None),
    ({0}, 'socket', ['1-5', '1-6', '1-7', '1-8']),
    ({3}, 'socket', None),
    ({3}, 'preferred', ['1-5', '1-6', '1-7', '1-8']),
])
def test_actual_a5_bound_and_free_inventory(environment, nodes, policy, expected):
    from ub_device_manager.domain.npu.device_selector import UbDeviceSelector

    system, devices = environment
    fe_ids = ['0000a', '0000b', '0000c', '0000d', '00005', '00006', '00007', '00008']
    inventory = []
    for index, fe_id in enumerate(fe_ids):
        add_fe(devices, fe_id, '0x03' if index < 4 else '0x01')
        bus = 'cc08-ffff-0-0-000000-000101ff0e940180' if index < 4 else ''
        inventory.append(npu(f'1-{index + 1}', fe_id, bus_guid=bus))
    topology = HostTopologyProvider(system, devices).get_topology(inventory)
    selector = UbDeviceSelector()
    if expected is None:
        with pytest.raises(ValueError, match='not enough free NPU'):
            selector.resolve_npu_devices(inventory, count=4, affinity_policy=policy,
                                         memory_nodes=nodes, topology=topology)
    else:
        selected = selector.resolve_npu_devices(inventory, count=4, affinity_policy=policy,
                                                memory_nodes=nodes, topology=topology)
        assert [device.id for device in selected] == expected
