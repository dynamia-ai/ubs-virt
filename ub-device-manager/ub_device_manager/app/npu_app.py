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
from typing import List

from fastapi import FastAPI
from loguru import logger

from ub_device_manager.app.models import (
    BindNpuDeviceRequest,
    BindNpuDeviceResult,
    Message,
    NpuDeviceInfo,
    UnbindNpuDeviceRequest,
)
from ub_device_manager.common.async_task_framework import AsyncTaskChain
from ub_device_manager.constants import (
    BIND_REQUEST_CONTEXT_KEY,
    BIND_RESULT_CONTEXT_KEY,
    NPU_LIST_CONTEXT_KEY,
    UNBIND_REQUEST_CONTEXT_KEY,
)
from ub_device_manager.exceptions import InvalidUnbindDeviceRequest
from ub_device_manager.domain.npu.npu_tasks import (
    BindNpuDeviceTask,
    QueryNpuTask,
    UnbindNpuDeviceTask,
)

app = FastAPI()


@app.get('/npu', response_model=List[NpuDeviceInfo])
async def get_npu_list() -> List[NpuDeviceInfo]:
    """
    List all NPU device information.
    """
    chain = (
        AsyncTaskChain()
        .apply_async_task(QueryNpuTask)
    )
    ctx = await chain.run_chain()
    return ctx.get(NPU_LIST_CONTEXT_KEY)


@app.post('/npu/bind', response_model=BindNpuDeviceResult, tags=['NPU'])
async def bind_npu_device(body: BindNpuDeviceRequest) -> BindNpuDeviceResult:
    """
    Bind NPU devices.
    """
    logger.info("Start bind UB devices request, upi: {}", body.upi)
    chain = (
        AsyncTaskChain()
        .with_context({BIND_REQUEST_CONTEXT_KEY: body})
        .apply_async_task(BindNpuDeviceTask)
    )
    return (await chain.run_chain()).get(BIND_RESULT_CONTEXT_KEY)


@app.post('/npu/unbind', response_model=Message, tags=['NPU'])
async def unbind_npu_device(body: UnbindNpuDeviceRequest) -> Message:
    """
    Unbind NPU devices.
    """
    logger.info("Start unbind UB devices request, bus_guid: {}", body.bus_guid)
    if not body.bus_guid.strip():
        raise InvalidUnbindDeviceRequest("bus_guid is required")

    chain = (
        AsyncTaskChain()
        .with_context({UNBIND_REQUEST_CONTEXT_KEY: body.bus_guid})
        .apply_async_task(UnbindNpuDeviceTask)
    )
    await chain.run_chain()
    return Message(msg="Unbind NPU devices successfully")
