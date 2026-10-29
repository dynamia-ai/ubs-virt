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
"""Unit tests for :mod:`ub_device_manager.exceptions`."""
import pytest

from ub_device_manager import exceptions as exc


class TestUbDmHttpException:
    async def test_default_detail_and_status_code(self):
        error = exc.UbDmHTTPException()

        assert error.status_code == 500
        assert error.detail == "an unexpected error"

    async def test_explicit_detail_and_headers_are_preserved(self):
        error = exc.UbDmHTTPException(detail="boom", headers={"X-Trace": "1"})

        assert error.detail == "boom"
        assert error.headers == {"X-Trace": "1"}

    async def test_every_subclass_inherits_the_base_type(self):
        assert issubclass(exc.InvalidCreateVmRequest, exc.UbDmHTTPException)
        assert issubclass(exc.SsuNotFound, exc.UbDmHTTPException)


@pytest.mark.parametrize(
    ("exception_cls", "status_code", "message"),
    [
        (exc.InvalidCreateVmRequest, 400, "invalid create vm request"),
        (exc.VmXmlTooLarge, 400, "vm xml exceeds the maximum allowed size"),
        (exc.VmXmlBuildFailed, 543, "vm xml build failed"),
        (exc.VmDefineStartFailed, 544, "vm define/start failed"),
        (exc.VmDeletionFailed, 531, "libvirt vm deletion failed"),
        (exc.LibvirtConnectionFailed, 532, "libvirt connection failed"),
        (exc.GetVmInfoFailed, 533, "get vm info failed"),
        (exc.UnbindDeviceFailed, 534, "call UBSE free_devices failed"),
        (exc.InvalidBindDeviceRequest, 400, "invalid bind ub device request"),
        (exc.ResolveNpuDevicesFailed, 545, "resolve npu devices failed"),
        (exc.InvalidUnbindDeviceRequest, 400, "invalid unbind ub device request"),
        (exc.UnbindDeviceNotFound, 548, "no ub devices found for the bus instance"),
        (exc.SsuNotFound, 404, "ssu resource not found"),
        (exc.InvalidSsuRequest, 400, "invalid ssu request"),
    ],
)
async def test_business_exception_codes_and_default_messages(exception_cls, status_code, message):
    error = exception_cls()

    assert error.status_code == status_code
    assert error.detail == message


async def test_detail_argument_overrides_default_message():
    error = exc.VmXmlBuildFailed(detail="custom detail")

    assert error.status_code == 543
    assert error.detail == "custom detail"
