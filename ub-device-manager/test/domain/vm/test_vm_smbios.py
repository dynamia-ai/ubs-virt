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

"""Tests for reading the host SMBIOS Type 131 table."""

import pytest
from ub_device_manager.exceptions import VmXmlBuildFailed
from subprocess import CalledProcessError, CompletedProcess
from unittest.mock import patch

from ub_device_manager.domain.vm.vm_smbios import VmSmbios, SmbiosType131


def make_output(flag=0):
    raw = bytearray(77)
    raw[0:4] = bytes([0x83, 77, 0x34, 0x00])
    raw[4] = flag
    raw[5:7] = (65535).to_bytes(2, 'little')
    raw[7] = 255
    raw[8] = 1
    raw[9:13] = (16383).to_bytes(4, 'little')
    raw[13:15] = (3).to_bytes(2, 'little')
    lines = [' '.join(f'{item:02X}' for item in raw[i:i+16]) for i in range(0, len(raw), 16)]
    return 'Handle 0x0034, DMI type 131, 77 bytes\nHeader and Data:\n\t' + '\n\t'.join(lines) + '\n\nStrings:\n\tUB INFO\n'


def test_parses_original_type131_fields():
    assert VmSmbios.parse_type131(make_output()) == SmbiosType131(0, 65535, 255, 1, 16383, 3)


def test_ignores_invalid_flag_and_missing_table():
    assert VmSmbios.parse_type131(make_output(flag=1)) is None
    assert VmSmbios.parse_type131('No SMBIOS type 131 structures found.') is None


def test_ignores_truncated_table():
    assert VmSmbios.parse_type131('Handle 0x0034, DMI type 131, 77 bytes\nHeader and Data:\n83 4D 34 00 00') is None


def test_reads_dmidecode():
    with patch('ub_device_manager.domain.vm.vm_smbios.subprocess.run', return_value=CompletedProcess([], 0, make_output(), '')) as run:
        assert VmSmbios().get_type131().pod_id == 65535
        assert run.call_args.args[0] == ['/usr/bin/sudo', '-n', '/usr/sbin/dmidecode', '-t', '131', '-u']


def test_missing_permission_fails_creation():
    with patch('ub_device_manager.domain.vm.vm_smbios.subprocess.run', side_effect=CalledProcessError(1, 'dmidecode')):
        with pytest.raises(VmXmlBuildFailed, match='Cannot read host SMBIOS Type 131'):
            VmSmbios().get_type131()


def test_invalid_type131_fails_creation():
    with patch('ub_device_manager.domain.vm.vm_smbios.subprocess.run',
               return_value=CompletedProcess([], 0, 'No SMBIOS type 131 structures found.', '')):
        with pytest.raises(VmXmlBuildFailed, match='missing or invalid'):
            VmSmbios().get_type131()
