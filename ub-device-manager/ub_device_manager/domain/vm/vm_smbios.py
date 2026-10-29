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
from __future__ import annotations

import re
import subprocess
from dataclasses import dataclass

from loguru import logger

from ub_device_manager.exceptions import VmXmlBuildFailed


# SMBIOS Type 131 (UB INFO) field layout:
# Offset       Field          Size (bytes)
# 0x00         Type           1
# 0x01         Length         1
# 0x02-0x03    Handle         2
# 0x04         Flag           1
# 0x05-0x06    PodId          2
# 0x07         SlotId         1
# 0x08         MeshType       1
# 0x09-0x0C    SuperPodId     4
# 0x0D-0x0E    ServedIndex    2
#
# Multi-byte fields are encoded in little-endian byte order.
# Flag 0 indicates valid Type 131 data.
SMBIOS_TYPE_131 = 0x83
SMBIOS_TYPE_OFFSET = 0x00
SMBIOS_LENGTH_OFFSET = 0x01
SMBIOS_FLAG_OFFSET = 0x04
SMBIOS_POD_ID_OFFSET = 0x05
SMBIOS_SLOT_ID_OFFSET = 0x07
SMBIOS_MESH_TYPE_OFFSET = 0x08
SMBIOS_SUPER_POD_ID_OFFSET = 0x09
SMBIOS_SERVED_INDEX_OFFSET = 0x0D
SMBIOS_TYPE_131_MIN_LENGTH = 0x0F
SMBIOS_FLAG_VALID = 0
SMBIOS_READ_TIMEOUT_SECONDS = 10


@dataclass(frozen=True)
class SmbiosType131:
    flag: int
    pod_id: int
    slot_id: int
    mesh_type: int
    super_pod_id: int
    served_index: int
    type_name: str = "UB INFO"


class VmSmbios:
    """Read the host's OEM SMBIOS Type 131 for NPU passthrough guests."""
    @staticmethod
    def parse_type131(output: str) -> SmbiosType131 | None:
        for section in re.split(r"(?=^Handle 0x[0-9A-Fa-f]+, DMI type )", output, flags=re.MULTILINE):
            if not re.search(r"^Handle 0x[0-9A-Fa-f]+, DMI type 131\b", section, re.MULTILINE):
                continue
            match = re.search(r"^\s*Header and Data:\s*$", section, re.MULTILINE)
            if match is None:
                continue
            byte_values = []
            for line in section[match.end():].splitlines():
                stripped = line.strip()
                if not stripped:
                    if byte_values:
                        break
                    continue
                if not re.fullmatch(r"(?:[0-9A-Fa-f]{2}\s+)*[0-9A-Fa-f]{2}", stripped):
                    break
                byte_values.extend(int(value, 16) for value in stripped.split())
            data = bytes(byte_values)
            if (
                len(data) < SMBIOS_TYPE_131_MIN_LENGTH
                    or data[SMBIOS_TYPE_OFFSET] != SMBIOS_TYPE_131
                    or data[SMBIOS_LENGTH_OFFSET] < SMBIOS_TYPE_131_MIN_LENGTH
                    or len(data) < data[SMBIOS_LENGTH_OFFSET]
            ):
                continue
            if data[SMBIOS_FLAG_OFFSET] != SMBIOS_FLAG_VALID:
                continue
            return SmbiosType131(
                flag=data[SMBIOS_FLAG_OFFSET],
                pod_id=int.from_bytes(data[SMBIOS_POD_ID_OFFSET:SMBIOS_SLOT_ID_OFFSET], "little"),
                slot_id=data[SMBIOS_SLOT_ID_OFFSET],
                mesh_type=data[SMBIOS_MESH_TYPE_OFFSET],
                super_pod_id=int.from_bytes(data[SMBIOS_SUPER_POD_ID_OFFSET:SMBIOS_SERVED_INDEX_OFFSET], "little"),
                served_index=int.from_bytes(data[SMBIOS_SERVED_INDEX_OFFSET:SMBIOS_TYPE_131_MIN_LENGTH], "little"),
            )
        return None

    def get_type131(self) -> SmbiosType131:
        """Require valid host Type 131 data for an NPU passthrough VM."""
        try:
            result = subprocess.run(
                ["/usr/bin/sudo", "-n", "/usr/sbin/dmidecode", "-t", "131", "-u"],
                capture_output=True,
                text=True,
                check=True,
                timeout=SMBIOS_READ_TIMEOUT_SECONDS,
            )
        except (OSError, subprocess.CalledProcessError, subprocess.TimeoutExpired) as exc:
            logger.error("Cannot read host SMBIOS Type 131: {}", exc)
            raise VmXmlBuildFailed("Cannot read host SMBIOS Type 131") from exc
        parsed = self.parse_type131(result.stdout)
        if parsed is None:
            logger.error("Host SMBIOS Type 131 is missing or not valid")
            raise VmXmlBuildFailed("Host SMBIOS Type 131 is missing or invalid")
        return parsed
