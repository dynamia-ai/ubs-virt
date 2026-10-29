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
from fastapi import HTTPException
from typing import Optional


class UbDmHTTPException(HTTPException):
    msg = "an unexpected error"
    code = 500
    def __init__(self, detail: Optional[str] = None, headers: Optional[dict] = None):
        super().__init__(status_code=self.__class__.code,
                         detail=detail or self.__class__.msg,
                         headers=headers)

class InvalidCreateVmRequest(UbDmHTTPException):
    msg = 'invalid create vm request'
    code = 400


class VmXmlTooLarge(InvalidCreateVmRequest):
    msg = 'vm xml exceeds the maximum allowed size'


class VmXmlBuildFailed(UbDmHTTPException):
    msg = 'vm xml build failed'
    code = 543


class VmDefineStartFailed(UbDmHTTPException):
    msg = 'vm define/start failed'
    code = 544


class VmDeletionFailed(UbDmHTTPException):
    msg = 'libvirt vm deletion failed'
    code = 531


class LibvirtConnectionFailed(UbDmHTTPException):
    msg = 'libvirt connection failed'
    code = 532


class GetVmInfoFailed(UbDmHTTPException):
    msg = 'get vm info failed'
    code = 533


class UnbindDeviceFailed(UbDmHTTPException):
    msg = 'call UBSE free_devices failed'
    code = 534


class InvalidBindDeviceRequest(UbDmHTTPException):
    msg = 'invalid bind ub device request'
    code = 400


class ResolveNpuDevicesFailed(UbDmHTTPException):
    msg = 'resolve npu devices failed'
    code = 545


class InvalidUnbindDeviceRequest(UbDmHTTPException):
    msg = 'invalid unbind ub device request'
    code = 400


class UnbindDeviceNotFound(UbDmHTTPException):
    msg = 'no ub devices found for the bus instance'
    code = 548


class SsuNotFound(UbDmHTTPException):
    msg = 'ssu resource not found'
    code = 404


class InvalidSsuRequest(UbDmHTTPException):
    msg = 'invalid ssu request'
    code = 400
