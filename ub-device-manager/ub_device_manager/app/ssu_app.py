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
from typing import List, Optional

from fastapi import FastAPI
from loguru import logger

from ub_device_manager.app.models import (
    BindSsuVfeReq,
    Message,
    NsConnectInfo,
    SsuAllocSpaceReq,
    SsuAttachResult,
    SsuInfo,
    SsuNsStatus,
    SsuPermReq,
    SsuSpaceReq,
    UnbindSsuVfeReq,
    VfeForSsu,
)
from ub_device_manager.common.async_task_framework import AsyncTaskChain
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
from ub_device_manager.domain.ssu.ssu_tasks import (
    AddSsuAccessPermTask,
    AllocSsuTask,
    BindSsuVfeTask,
    FreeSsuSpaceTask,
    GetNsConnectInfoTask,
    GetSsuListTask,
    GetSsuVfeListTask,
    RemoveSsuAccessPermTask,
    ShowSsuNsStatusTask,
    ShowSsuTask,
    SsuSpaceAttachTask,
    SsuSpaceDetachTask,
    UnbindSsuVfeTask,
)

app = FastAPI()


@app.post('/ssu/alloc', response_model=SsuInfo, tags=['SSU'])
async def ssu_alloc(body: SsuAllocSpaceReq) -> SsuInfo:
    """
    Allocate SSU storage space.
    """
    logger.info("Start ssu alloc request, name: {}", body.name)
    chain = (
        AsyncTaskChain()
        .with_context({SSU_ALLOC_REQUEST_CONTEXT_KEY: body})
        .apply_async_task(AllocSsuTask)
    )
    return (await chain.run_chain()).get(SSU_ALLOC_RESULT_CONTEXT_KEY)


@app.get('/ssu', response_model=List[SsuInfo], tags=['SSU'])
async def get_ssu_list() -> List[SsuInfo]:
    """
    List all allocated storage spaces.
    """
    logger.info("Start get ssu list request")
    chain = (
        AsyncTaskChain()
        .apply_async_task(GetSsuListTask)
    )
    return (await chain.run_chain()).get(SSU_LIST_CONTEXT_KEY)


@app.get('/ssu/{name}', response_model=SsuInfo, tags=['SSU'])
async def show_ssu_alloc_info(name: str) -> SsuInfo:
    """
    Get allocated storage space information by name.
    """
    logger.info("Start show ssu alloc info request, name: {}.", name)
    chain = (
        AsyncTaskChain()
        .with_context({SSU_SHOW_REQUEST_CONTEXT_KEY: name})
        .apply_async_task(ShowSsuTask)
    )
    return (await chain.run_chain()).get(SSU_SHOW_RESULT_CONTEXT_KEY)


@app.delete('/ssu/{name}', response_model=Message, tags=['SSU'])
async def free_ssu(name: str) -> Message:
    """
    Free SSU storage space.
    """
    logger.info("Start free ssu request, name: {}", name)
    chain = (
        AsyncTaskChain()
        .with_context({SSU_FREE_REQUEST_CONTEXT_KEY: name})
        .apply_async_task(FreeSsuSpaceTask)
    )
    await chain.run_chain()
    return Message(msg=f"Freed {name} successfully.")


@app.get('/ssu-ns/connect-info', response_model=List[NsConnectInfo], tags=['SSU'])
async def get_ns_connnect_info(
        name: str, vfe_guid: Optional[str] = None
) -> List[NsConnectInfo]:
    """
    Query NVMe connection information for a storage space on a specified VFE.
    """
    logger.info("Start get ns connect info request, name: {}, vfe_guid: {}", name, vfe_guid)
    chain = (
        AsyncTaskChain()
        .with_context({SSU_NS_CONNECT_INFO_REQUEST_CONTEXT_KEY: (name, vfe_guid)})
        .apply_async_task(GetNsConnectInfoTask)
    )
    return (await chain.run_chain()).get(SSU_NS_CONNECT_INFO_RESULT_CONTEXT_KEY)


@app.get('/ssu-ns/{name}', response_model=List[SsuNsStatus], tags=['SSU'])
async def show_ssu_ns_status(name: str) -> List[SsuNsStatus]:
    """
    Get namespace information for a storage space.
    """
    logger.info("Start show ssu ns status request, name: {}", name)
    chain = (
        AsyncTaskChain()
        .with_context({SSU_NS_STATUS_REQUEST_CONTEXT_KEY: name})
        .apply_async_task(ShowSsuNsStatusTask)
    )
    return (await chain.run_chain()).get(SSU_NS_STATUS_RESULT_CONTEXT_KEY)


@app.post('/ssu-vfe/bind', response_model=Message, tags=['SSU'])
async def bind_ssu_vfe_bus(body: BindSsuVfeReq) -> Message:
    """
    Bind a specified VFE to a bus.
    """
    logger.info("Start bind ssu vfe bus request, vfe_guid: {}", body.vfe_guid)
    chain = (
        AsyncTaskChain()
        .with_context({SSU_VFE_BIND_REQUEST_CONTEXT_KEY: body})
        .apply_async_task(BindSsuVfeTask)
    )
    await chain.run_chain()
    return Message(msg="Bind SSU VFE successfully")


@app.get('/ssu-vfe', response_model=List[VfeForSsu], tags=['SSU'])
async def get_ssu_vfe_list() -> List[VfeForSsu]:
    """
    List all SSU-specific VFEs.
    """
    logger.info("Start get ssu vfe list request")
    chain = (
        AsyncTaskChain()
        .apply_async_task(GetSsuVfeListTask)
    )
    return (await chain.run_chain()).get(SSU_VFE_LIST_CONTEXT_KEY)


@app.post('/ssu-vfe/unbind', response_model=Message, tags=['SSU'])
async def unbind_ssu_vfe(body: UnbindSsuVfeReq) -> Message:
    """
    Unbind a specified VFE from a bus GUID.
    """
    logger.info("Start unbind ssu vfe request, vfe_guid: {}", body.vfe_guid)
    chain = (
        AsyncTaskChain()
        .with_context({SSU_VFE_UNBIND_REQUEST_CONTEXT_KEY: body})
        .apply_async_task(UnbindSsuVfeTask)
    )
    await chain.run_chain()
    return Message(msg="Unbind SSU VFE successfully")


@app.post('/ssu/add-perm', response_model=Message, tags=['SSU'])
async def add_ssu_perm(body: SsuPermReq) -> Message:
    """
    Add SSU access permission.
    """
    logger.info("Start add ssu perm request, name: {}", body.name)
    chain = (
        AsyncTaskChain()
        .with_context({SSU_PERM_REQUEST_CONTEXT_KEY: body})
        .apply_async_task(AddSsuAccessPermTask)
    )
    await chain.run_chain()
    return Message(msg="Add SSU access permission successfully")


@app.post('/ssu/rm-perm', response_model=Message, tags=['SSU'])
async def remove_ssu_perm(body: SsuPermReq) -> Message:
    """
    Remove SSU access permission.
    """
    logger.info("Start remove ssu perm request, name: {}", body.name)
    chain = (
        AsyncTaskChain()
        .with_context({SSU_PERM_REQUEST_CONTEXT_KEY: body})
        .apply_async_task(RemoveSsuAccessPermTask)
    )
    await chain.run_chain()
    return Message(msg="Remove SSU access permission successfully")


@app.post('/ssu/attach', response_model=SsuAttachResult, tags=['SSU'])
async def ssu_attach(body: SsuSpaceReq) -> SsuAttachResult:
    """
    Attach an allocated storage space.
    """
    logger.info("Start ssu attach request, name: {}", body.name)
    chain = (
        AsyncTaskChain()
        .with_context({SSU_SPACE_ATTACH_REQUEST_CONTEXT_KEY: body})
        .apply_async_task(SsuSpaceAttachTask)
    )
    dev_paths = (await chain.run_chain()).get(SSU_SPACE_ATTACH_RESULT_CONTEXT_KEY)
    return SsuAttachResult(dev_paths=dev_paths)


@app.post('/ssu/detach', response_model=Message, tags=['SSU'])
async def ssu_detach(body: SsuSpaceReq) -> Message:
    """
    Detach an allocated storage space.
    """
    logger.info("Start ssu detach request, name: {}", body.name)
    chain = (
        AsyncTaskChain()
        .with_context({SSU_SPACE_DETACH_REQUEST_CONTEXT_KEY: body})
        .apply_async_task(SsuSpaceDetachTask)
    )
    await chain.run_chain()
    return Message(msg="Detach SSU storage space successfully")
