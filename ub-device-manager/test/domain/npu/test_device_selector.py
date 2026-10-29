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
"""Unit tests for :mod:`ub_device_manager.domain.npu.device_selector`."""
import pytest

from ub_device_manager.domain.npu.device_selector import (
    DEFAULT_NIC_TYPE,
    UbDevice,
    UbDeviceSelector,
    UbDeviceType,
)
from ub_device_manager.domain.npu.npu_client import AffinityDeviceInfo, UbDeviceInfo


def make_device(device_id, device_type="NPU", bus_guid=None, affinity=()):
    return UbDeviceInfo(
        id=device_id,
        type=device_type,
        guid=f"guid-{device_id}",
        bus_guid=bus_guid,
        affinity_devs=[AffinityDeviceInfo(device_id=item[0], device_type=item[1]) for item in affinity],
    )


@pytest.fixture
def selector():
    return UbDeviceSelector()


class TestExplicitSelection:
    async def test_resolves_explicit_free_npus(self, selector):
        inventory = [make_device("0-1"), make_device("0-2")]

        selected = selector.resolve_npu_devices(inventory, ids=["0-2"])

        assert selected == [UbDevice(type="NPU", id="0-2", guid="guid-0-2")]

    async def test_missing_device_raises(self, selector):
        with pytest.raises(ValueError, match="UB device not found"):
            selector.resolve_npu_devices([], ids=["9-9"])

    async def test_already_bound_device_raises(self, selector):
        inventory = [make_device("0-1", bus_guid="bus-1")]

        with pytest.raises(ValueError, match="already bound"):
            selector.resolve_npu_devices(inventory, ids=["0-1"])


class TestCountSelection:
    async def test_selects_first_free_npus(self, selector):
        inventory = [
            make_device("0-1"),
            make_device("0-2", bus_guid="bus"),
            make_device("0-3"),
        ]

        selected = selector.resolve_npu_devices(inventory, count=2)

        assert [device.id for device in selected] == ["0-1", "0-3"]

    async def test_not_enough_free_npus_raises(self, selector):
        inventory = [make_device("0-1")]

        with pytest.raises(ValueError, match="not enough free NPU"):
            selector.resolve_npu_devices(inventory, count=2)

    async def test_non_npu_devices_are_ignored(self, selector):
        inventory = [make_device("0-1", device_type="NIC_VFE"), make_device("0-2")]

        selected = selector.resolve_npu_devices(inventory, count=1)

        assert [device.id for device in selected] == ["0-2"]


class TestNicAffinity:
    async def test_need_nic_appends_affinity_nic(self, selector):
        inventory = [
            make_device("0-1", affinity=[("1-1", "NIC_VFE")]),
            make_device("1-1", device_type="NIC_VFE"),
        ]

        selected = selector.resolve_npu_devices(inventory, ids=["0-1"], need_nic=True)

        assert [device.type for device in selected] == ["NPU", "NIC_VFE"]
        assert selected[1].id == "1-1"

    async def test_default_nic_type_is_vfe(self):
        assert DEFAULT_NIC_TYPE == UbDeviceType.NIC_VFE

    async def test_explicit_npu_without_affinity_nic_raises(self, selector):
        inventory = [make_device("0-1")]

        with pytest.raises(ValueError, match="no available affinity"):
            selector.resolve_npu_devices(inventory, ids=["0-1"], need_nic=True)

    async def test_count_selection_skips_npus_without_usable_nic(self, selector):
        inventory = [
            make_device("0-1"),
            make_device("0-2", affinity=[("1-2", "NIC_VFE")]),
            make_device("1-2", device_type="NIC_VFE"),
        ]

        selected = selector.resolve_npu_devices(inventory, count=1, need_nic=True)

        assert [device.id for device in selected] == ["0-2", "1-2"]

    async def test_count_selection_reports_insufficient_affinity_nics(self, selector):
        inventory = [make_device("0-1")]

        with pytest.raises(ValueError, match="affinity NIC_VFE"):
            selector.resolve_npu_devices(inventory, count=1, need_nic=True)

    async def test_bound_affinity_nic_is_not_reused(self, selector):
        inventory = [
            make_device("0-1", affinity=[("1-1", "NIC_VFE")]),
            make_device("1-1", device_type="NIC_VFE", bus_guid="bus"),
        ]

        with pytest.raises(ValueError, match="affinity NIC_VFE"):
            selector.resolve_npu_devices(inventory, count=1, need_nic=True)

    async def test_pfe_affinity_is_ignored(self, selector):
        inventory = [
            make_device("0-1", affinity=[("1-1", "NIC_PFE")]),
            make_device("1-1", device_type="NIC_PFE"),
        ]

        with pytest.raises(ValueError, match="affinity NIC_VFE"):
            selector.resolve_npu_devices(inventory, count=1, need_nic=True)


async def test_ub_device_type_values():
    assert UbDeviceType.NPU == "NPU"
    assert UbDeviceType.NIC_PFE == "NIC_PFE"
    assert UbDeviceType.NIC_VFE == "NIC_VFE"
