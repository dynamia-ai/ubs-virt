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
"""Minimal fake ``libvirt`` module used by the unit tests.

The real libvirt binding is a native extension that is not available on the
development or pipeline machines.  Any test that touches
``ub_device_manager.domain.vm.vm_client`` must install this stub before the
module is imported, because ``vm_client`` evaluates ``libvirt.virConnect`` in
its annotations at import time.
"""
import sys
import types

_STATE_CONSTANTS = {
    "VIR_DOMAIN_NOSTATE": 0,
    "VIR_DOMAIN_RUNNING": 1,
    "VIR_DOMAIN_BLOCKED": 2,
    "VIR_DOMAIN_PAUSED": 3,
    "VIR_DOMAIN_SHUTDOWN": 4,
    "VIR_DOMAIN_SHUTOFF": 5,
    "VIR_DOMAIN_CRASHED": 6,
    "VIR_DOMAIN_PMSUSPENDED": 7,
    "VIR_DOMAIN_UNDEFINE_NVRAM": 1 << 1,
}


def install():
    """Install the fake ``libvirt`` module and return it (idempotent)."""
    existing = sys.modules.get("libvirt")
    if existing is not None and getattr(existing, "_is_stub", False):
        return existing

    module = types.ModuleType("libvirt")
    for name, value in _STATE_CONSTANTS.items():
        setattr(module, name, value)

    class virConnect:  # noqa: N801 - mirrors the native libvirt type name
        """Placeholder connection type, used only for type annotations."""

    def open(uri):  # noqa: A001 - mirrors the native libvirt.open API
        raise RuntimeError("libvirt stub: libvirt.open must be patched in tests")

    module.virConnect = virConnect
    module.open = open
    module._is_stub = True
    sys.modules["libvirt"] = module
    return module
