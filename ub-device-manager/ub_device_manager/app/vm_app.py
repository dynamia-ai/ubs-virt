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
from fastapi import FastAPI
from loguru import logger

from ub_device_manager.app.models import (
    CreateVmRequest,
    Message,
)
from ub_device_manager.constants import (
    DELETE_VM_CONTEXT_KEY,
)
from ub_device_manager.common.async_task_framework import AsyncTaskChain
from ub_device_manager.constants import CREATE_VM_REQUEST_CONTEXT_KEY
from ub_device_manager.domain.npu.npu_tasks import (
    BindNpuDeviceTask,
    UnbindNpuDeviceTask,
)
from ub_device_manager.domain.vm.vm_tasks import (
    BuildAndStartVmTask,
    DeleteVmTask,
    PrepareVmXmlTask,
)

app = FastAPI()
VM_BIND_LOCK_NAME = "vm_bind_npu_device"


@app.post('/vm/create', response_model=Message, tags=['VM'])
async def create_vm(body: CreateVmRequest) -> Message:
    """
    Create UB VM
    """
    logger.info("Start create VM request, upi: {}", body.upi)

    chain = (
        AsyncTaskChain()
        .with_context({CREATE_VM_REQUEST_CONTEXT_KEY: body})
        .apply_async_task(PrepareVmXmlTask)
        .apply_acquire_lock(VM_BIND_LOCK_NAME)
        .apply_async_task(BindNpuDeviceTask)
        .apply_release_lock(VM_BIND_LOCK_NAME)
        .apply_async_task(BuildAndStartVmTask)
    )
    await chain.run_chain()
    return Message(msg="Create VM successfully")


@app.delete('/vm/{name}', response_model=Message, tags=['VM'])
async def delete_vm(name: str) -> Message:
    """
    Delete a VM by name.
    """
    logger.info("Start delete VM request, name: {}", name)
    chain = (
        AsyncTaskChain()
        .with_context({DELETE_VM_CONTEXT_KEY: name})
        .apply_async_task(DeleteVmTask)
        .apply_async_task(UnbindNpuDeviceTask)
    )
    await chain.run_chain()
    return Message(msg=f"Deleted {name} successfully.")
