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
from ub_device_manager.app.models import (
    BindSsuVfeReq,
    SsuAllocSpaceReq,
    SsuAllocStrategy,
    SsuPermReq,
    SsuSpaceReq,
    UnbindSsuVfeReq,
)
from ub_device_manager.common.async_task_framework import AsyncTask
from ub_device_manager.exceptions import InvalidSsuRequest
from ub_device_manager.constants import (
    SSU_ALLOC_REQUEST_CONTEXT_KEY,
    SSU_ALLOC_RESULT_CONTEXT_KEY,
    SSU_FREE_REQUEST_CONTEXT_KEY,
    SSU_LIST_CONTEXT_KEY,
    SSU_NS_CONNECT_INFO_REQUEST_CONTEXT_KEY,
    SSU_NS_CONNECT_INFO_RESULT_CONTEXT_KEY,
    SSU_NS_STATUS_REQUEST_CONTEXT_KEY,
    SSU_NS_STATUS_RESULT_CONTEXT_KEY,
    SSU_PERM_REQUEST_CONTEXT_KEY,
    SSU_SHOW_REQUEST_CONTEXT_KEY,
    SSU_SHOW_RESULT_CONTEXT_KEY,
    SSU_SPACE_ATTACH_REQUEST_CONTEXT_KEY,
    SSU_SPACE_ATTACH_RESULT_CONTEXT_KEY,
    SSU_SPACE_DETACH_REQUEST_CONTEXT_KEY,
    SSU_VFE_BIND_REQUEST_CONTEXT_KEY,
    SSU_VFE_LIST_CONTEXT_KEY,
    SSU_VFE_UNBIND_REQUEST_CONTEXT_KEY,
)
from ub_device_manager.domain.ssu.ssu_client import SsuClient


class AllocSsuTask(AsyncTask):
    def __init__(self):
        self.ssu_client = SsuClient()

    @staticmethod
    def _validate_request(request: SsuAllocSpaceReq) -> None:
        if request.ns_size_gb < 1:
            raise InvalidSsuRequest('ns_size_gb must be greater than or equal to 1')
        if request.ns_num == 1 and request.strategy == SsuAllocStrategy.integer_0:
            raise InvalidSsuRequest('Strategy cannot be STRIPED when ns_num is 1!')

    async def execute(self) -> None:
        request: SsuAllocSpaceReq = self.context.get(SSU_ALLOC_REQUEST_CONTEXT_KEY)
        self._validate_request(request)
        result = await self.ssu_client.ssu_alloc(request)
        self.context.set(SSU_ALLOC_RESULT_CONTEXT_KEY, result)


class GetSsuListTask(AsyncTask):
    def __init__(self):
        self.ssu_client = SsuClient()

    async def execute(self) -> None:
        result = await self.ssu_client.get_ssu_list()
        self.context.set(SSU_LIST_CONTEXT_KEY, result)


class ShowSsuTask(AsyncTask):
    def __init__(self):
        self.ssu_client = SsuClient()

    async def execute(self) -> None:
        name: str = self.context.get(SSU_SHOW_REQUEST_CONTEXT_KEY)
        result = await self.ssu_client.show_ssu_alloc_info(name)
        self.context.set(SSU_SHOW_RESULT_CONTEXT_KEY, result)


class FreeSsuSpaceTask(AsyncTask):
    def __init__(self):
        self.ssu_client = SsuClient()

    async def execute(self) -> None:
        ssu_name: str = self.context.get(SSU_FREE_REQUEST_CONTEXT_KEY)
        await self.ssu_client.ssu_free(ssu_name)


class GetNsConnectInfoTask(AsyncTask):
    def __init__(self):
        self.ssu_client = SsuClient()

    async def execute(self) -> None:
        name, vfe_guid = self.context.get(SSU_NS_CONNECT_INFO_REQUEST_CONTEXT_KEY)
        result = await self.ssu_client.get_ns_connnect_info(name, vfe_guid)
        self.context.set(SSU_NS_CONNECT_INFO_RESULT_CONTEXT_KEY, result)


class ShowSsuNsStatusTask(AsyncTask):
    def __init__(self):
        self.ssu_client = SsuClient()

    async def execute(self) -> None:
        name: str = self.context.get(SSU_NS_STATUS_REQUEST_CONTEXT_KEY)
        result = await self.ssu_client.show_ssu_ns_status(name)
        self.context.set(SSU_NS_STATUS_RESULT_CONTEXT_KEY, result)


class BindSsuVfeTask(AsyncTask):
    def __init__(self):
        self.ssu_client = SsuClient()

    async def execute(self) -> None:
        request: BindSsuVfeReq = self.context.get(SSU_VFE_BIND_REQUEST_CONTEXT_KEY)
        await self.ssu_client.bind_ssu_vfe_bus(request)


class GetSsuVfeListTask(AsyncTask):
    def __init__(self):
        self.ssu_client = SsuClient()

    async def execute(self) -> None:
        result = await self.ssu_client.get_ssu_vfe_list()
        self.context.set(SSU_VFE_LIST_CONTEXT_KEY, result)


class UnbindSsuVfeTask(AsyncTask):
    def __init__(self):
        self.ssu_client = SsuClient()

    async def execute(self) -> None:
        request: UnbindSsuVfeReq = self.context.get(SSU_VFE_UNBIND_REQUEST_CONTEXT_KEY)
        await self.ssu_client.unbind_ssu_vfe_bus(request)


class AddSsuAccessPermTask(AsyncTask):
    def __init__(self):
        self.ssu_client = SsuClient()

    async def execute(self) -> None:
        request: SsuPermReq = self.context.get(SSU_PERM_REQUEST_CONTEXT_KEY)
        await self.ssu_client.add_ssu_access_perm(request)


class RemoveSsuAccessPermTask(AsyncTask):
    def __init__(self):
        self.ssu_client = SsuClient()

    async def execute(self) -> None:
        request: SsuPermReq = self.context.get(SSU_PERM_REQUEST_CONTEXT_KEY)
        await self.ssu_client.remove_ssu_access_perm(request)


class SsuSpaceAttachTask(AsyncTask):
    def __init__(self):
        self.ssu_client = SsuClient()

    async def execute(self) -> None:
        request: SsuSpaceReq = self.context.get(SSU_SPACE_ATTACH_REQUEST_CONTEXT_KEY)
        result = await self.ssu_client.ssu_space_attach(request)
        self.context.set(SSU_SPACE_ATTACH_RESULT_CONTEXT_KEY, result)


class SsuSpaceDetachTask(AsyncTask):
    def __init__(self):
        self.ssu_client = SsuClient()

    async def execute(self) -> None:
        request: SsuSpaceReq = self.context.get(SSU_SPACE_DETACH_REQUEST_CONTEXT_KEY)
        await self.ssu_client.ssu_space_detach(request)



