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
from contextlib import ExitStack
from unittest.mock import patch

from fastapi.testclient import TestClient
import pytest

from test.test_end_to_end import FakeConnection, FakeDomain, FakeUbseDevice
from test.domain.vm.test_vm_memory_affinity import memory_xml
from test.domain.npu.test_affinity_policy import TOPOLOGY
from ub_device_manager.common.factory import create_app
from ub_device_manager.domain.npu.host_topology import HostTopologyProvider
from ub_device_manager.domain.vm.vm_smbios import SmbiosType131
from ub_device_manager.exceptions import AffinityTopologyUnavailable


@pytest.fixture
def flow():
    conn = FakeConnection(define_domain=FakeDomain())
    with ExitStack() as stack:
        stack.enter_context(patch('libvirt.open', return_value=conn))
        get = stack.enter_context(patch('ubse.ubs_engine_npu.get_host_ub_devices', return_value=[
            FakeUbseDevice('NPU', f'npu{i}', f'guid-{i}') for i in range(8)]))
        alloc = stack.enter_context(patch('ubse.ubs_engine_npu.alloc_devices', return_value=('bus-1', None)))
        stack.enter_context(patch('ubse.ubs_engine_npu.query_uba_tid_size', return_value=(1, 2, 3)))
        free = stack.enter_context(patch('ubse.ubs_engine_npu.free_devices'))
        topology = stack.enter_context(patch.object(HostTopologyProvider, 'get_topology', return_value=TOPOLOGY))
        stack.enter_context(patch('ub_device_manager.domain.vm.vm_smbios.VmSmbios.get_type131',
                                 return_value=SmbiosType131(0, 65535, 255, 1, 16383, 3)))
        client = stack.enter_context(TestClient(create_app(), raise_server_exceptions=False))
        yield client, alloc, get, topology, conn, free


def payload(policy='required', nodes='1', **kwargs):
    xml = memory_xml(f'<memory mode="strict" nodeset="{nodes}"/>')
    xml = xml.replace('<domain>', '<domain><os><type>hvm</type></os>')
    return {'upi': '0x1', 'xml_text': xml, 'npu_count': 1, 'affinity_policy': policy, **kwargs}


@pytest.mark.parametrize(('policy', 'nodes', 'first'), [
    ('required', '1', 'npu4'), ('required', '3', 'npu0'),
    ('socket', '0', 'npu4'), ('socket', '2', 'npu0'),
])
def test_http_policy_reaches_actual_allocation(flow, policy, nodes, first):
    client, alloc, _, _, conn, _ = flow
    response = client.post('/vm/create', json=payload(policy, nodes))
    assert response.status_code == 200, response.text
    assert alloc.call_args.kwargs['device_list'] == [{'device_id': first, 'device_type': 'NPU'}]
    assert '<memory mode="strict"' in conn.defined_xml


def test_preferred_fills_and_preserves_same_socket_first_order(flow):
    client, alloc, *_ = flow
    response = client.post('/vm/create', json=payload('preferred', npu_count=8))
    assert response.status_code == 200, response.text
    assert [d['device_id'] for d in alloc.call_args.kwargs['device_list']] == [
        'npu4', 'npu5', 'npu6', 'npu7', 'npu0', 'npu1', 'npu2', 'npu3']


@pytest.mark.parametrize('policy', ['required', 'socket'])
def test_hard_shortage_never_calls_allocation(flow, policy):
    client, alloc, *_ = flow
    response = client.post('/vm/create', json=payload(policy, npu_count=8))
    assert response.status_code == 545
    alloc.assert_not_called()


def test_bad_memory_xml_fails_before_inventory_or_allocation(flow):
    client, alloc, get, topology, *_ = flow
    response = client.post('/vm/create', json=payload(xml_text='<domain/>'))
    assert response.status_code == 400
    get.assert_not_called()
    topology.assert_not_called()
    alloc.assert_not_called()


@pytest.mark.parametrize('policy', ['required', 'socket', 'preferred'])
@pytest.mark.parametrize('count', [None, 1])
@pytest.mark.parametrize('ids', [[], ['npu0']])
def test_policy_with_explicit_ids_is_rejected_before_external_calls(flow, policy, count, ids):
    client, alloc, get, topology, _, free = flow
    request = payload(policy, npu_count=count, npu_ids=ids)
    with (
        patch('ub_device_manager.domain.vm.vm_smbios.VmSmbios.get_type131') as smbios,
        patch('ub_device_manager.domain.vm.vm_tasks.PrepareVmXmlTask._load_and_validate_xml') as load_xml,
    ):
        response = client.post('/vm/create', json=request)
    assert response.status_code == 400, response.text
    assert 'affinity_policy cannot be used with npu_ids' in response.text
    load_xml.assert_not_called()
    smbios.assert_not_called()
    get.assert_not_called()
    topology.assert_not_called()
    alloc.assert_not_called()
    free.assert_not_called()


@pytest.mark.parametrize('policy', ['', 'legacy', 'bogus'])
def test_invalid_policy_is_rejected_by_http_model(flow, policy):
    client, alloc, *_ = flow
    assert client.post('/vm/create', json=payload(policy)).status_code == 422
    alloc.assert_not_called()


@pytest.mark.parametrize('policy', ['required', 'socket', 'preferred'])
def test_policy_without_npu_count_is_rejected(flow, policy):
    client, alloc, get, topology, *_ = flow
    response = client.post('/vm/create', json=payload(policy, npu_count=None))
    assert response.status_code == 400, response.text
    assert 'affinity_policy requires npu_count' in response.text
    get.assert_not_called()
    topology.assert_not_called()
    alloc.assert_not_called()


def test_topology_failure_is_reported_without_binding(flow):
    client, alloc, _, topology, *_ = flow
    topology.side_effect = AffinityTopologyUnavailable('UB FE NUMA file unavailable')
    response = client.post('/vm/create', json=payload())
    assert response.status_code == 503
    alloc.assert_not_called()


@pytest.mark.parametrize('policy', [None, 'omitted'])
def test_old_calls_skip_new_xml_checks_and_topology(flow, policy):
    client, alloc, _, topology, *_ = flow
    request = payload(xml_text='<domain><os><type>hvm</type></os></domain>')
    if policy == 'omitted':
        del request['affinity_policy']
    else:
        request['affinity_policy'] = None
    assert client.post('/vm/create', json=request).status_code == 200
    topology.assert_not_called()
    assert alloc.call_args.kwargs['device_list'][0]['device_id'] == 'npu0'


def test_start_failure_still_releases_selected_devices(flow):
    client, _, _, _, conn, free = flow
    conn._define_domain.create = lambda: (_ for _ in ()).throw(RuntimeError('start failed'))
    assert client.post('/vm/create', json=payload()).status_code == 544
    free.assert_called_once_with(bus_instance_guid='bus-1',
                                 device_list=[{'device_id': 'npu4', 'device_type': 'NPU'}])


@pytest.mark.parametrize(('nodes', 'policy', 'count', 'expected'), [
    ('1,2', 'preferred', 4, ['npu0', 'npu1', 'npu2', 'npu3']),
    ('1,2', 'preferred', 8, ['npu0', 'npu1', 'npu2', 'npu3', 'npu4', 'npu5', 'npu6', 'npu7']),
    ('1,2', 'socket', 8, ['npu0', 'npu1', 'npu2', 'npu3', 'npu4', 'npu5', 'npu6', 'npu7']),
    ('1,3', 'required', 8, ['npu0', 'npu1', 'npu2', 'npu3', 'npu4', 'npu5', 'npu6', 'npu7']),
    ('0,2', 'preferred', 8, ['npu0', 'npu1', 'npu2', 'npu3', 'npu4', 'npu5', 'npu6', 'npu7']),
])
def test_cross_socket_host_nodes_reach_allocation(flow, nodes, policy, count, expected):
    client, allocate, _, _, conn, _ = flow
    response = client.post('/vm/create', json=payload(policy, nodes, npu_count=count))
    assert response.status_code == 200, response.text
    assert [item['device_id'] for item in allocate.call_args.kwargs['device_list']] == expected
    assert f'nodeset="{nodes}"' in conn.defined_xml


def test_required_does_not_borrow_from_same_socket_for_multiple_memory_nodes(flow):
    client, allocate, *_ = flow
    response = client.post('/vm/create', json=payload('required', '1,2', npu_count=8))
    assert response.status_code == 545
    allocate.assert_not_called()


@pytest.mark.parametrize('policy', [None, 'omitted'])
def test_explicit_ids_without_policy_preserve_original_behavior(flow, policy):
    client, allocate, _, topology, *_ = flow
    request = payload(npu_count=None, npu_ids=['npu4', 'npu0'],
                      xml_text='<domain><os><type>hvm</type></os></domain>')
    if policy == 'omitted':
        del request['affinity_policy']
    else:
        request['affinity_policy'] = None
    response = client.post('/vm/create', json=request)
    assert response.status_code == 200, response.text
    topology.assert_not_called()
    assert [item['device_id'] for item in allocate.call_args.kwargs['device_list']] == ['npu4', 'npu0']
