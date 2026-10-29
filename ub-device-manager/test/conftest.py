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
"""Shared pytest configuration and fixtures.

The native ``libvirt`` and ``ubse`` runtimes are not installed on development
or pipeline machines, so their fakes are installed before the test modules are
imported (pytest loads ``conftest.py`` first).
"""
import asyncio
import inspect
import sys

import pytest

from test.libvirt_stub import install as _install_libvirt_stub
from test.ubse_stub import install as _install_ubse_stub

_install_libvirt_stub()
_install_ubse_stub()


@pytest.hookimpl(tryfirst=True)
def pytest_pyfunc_call(pyfuncitem):
    """Run ``async def`` tests without requiring the pytest-asyncio plugin.

    Each coroutine test runs in its own event loop via ``asyncio.run``.  This
    keeps the suite runnable with a plain pytest interpreter (for example the
    one picked by an IDE), independent of any async plugin being installed.
    """
    test_function = pyfuncitem.obj
    if not inspect.iscoroutinefunction(test_function):
        return None

    funcargs = pyfuncitem.funcargs
    testargs = {name: funcargs[name] for name in pyfuncitem._fixtureinfo.argnames}
    asyncio.run(test_function(**testargs))
    return True


@pytest.fixture
def ubse_modules():
    """Expose the fake ubse submodules so tests can patch them in place."""
    return {
        "npu": sys.modules["ubse.ubs_engine_npu"],
        "ssu": sys.modules["ubse.ubs_engine_ssu"],
        "ssu_models": sys.modules["ubse.models.ubs_engine_model_ssu"],
    }
