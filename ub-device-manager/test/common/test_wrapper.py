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
"""Unit tests for :mod:`ub_device_manager.common.wrapper`."""
import asyncio

import pytest
from loguru import logger
from pydantic import BaseModel

from ub_device_manager.common.wrapper import try_catch_log


@pytest.fixture
def captured_logs():
    messages = []
    sink_id = logger.add(lambda message: messages.append(message), format="{message}")
    try:
        yield messages
    finally:
        logger.remove(sink_id)


class Payload(BaseModel):
    name: str
    count: int


class Record:
    def __init__(self):
        self.public = "shown"
        self._private = "hidden"


@try_catch_log
def sync_ok(value, extra=0):
    return value + extra


@try_catch_log
def echo(value):
    return value


@try_catch_log
def sync_fail():
    raise ValueError("sync boom")


@try_catch_log
async def async_ok(value):
    await asyncio.sleep(0)
    return value * 2


@try_catch_log
async def async_fail():
    await asyncio.sleep(0)
    raise RuntimeError("async boom")


class Service:
    @try_catch_log
    def method(self, value):
        return value

    @classmethod
    @try_catch_log
    def class_method(cls, value):
        return value


async def test_sync_function_result_is_returned(captured_logs):
    assert sync_ok(1, extra=2) == 3
    assert any("Start to call sync_ok" in message for message in captured_logs)
    assert any("Call sync_ok successful" in message for message in captured_logs)


async def test_sync_exception_is_logged_and_reraised(captured_logs):
    with pytest.raises(ValueError, match="sync boom"):
        sync_fail()

    assert any("Call sync_fail failed" in message for message in captured_logs)


async def test_async_function_result_is_returned(captured_logs):
    assert await async_ok(21) == 42
    assert any("Start to call async_ok" in message for message in captured_logs)


async def test_async_exception_is_logged_and_reraised(captured_logs):
    with pytest.raises(RuntimeError, match="async boom"):
        await async_fail()

    assert any("Call async_fail failed" in message for message in captured_logs)


async def test_wrapper_preserves_function_metadata():
    assert sync_ok.__name__ == "sync_ok"
    assert async_ok.__name__ == "async_ok"


async def test_pydantic_arguments_are_rendered(captured_logs):
    echo(Payload(name="x", count=1))

    assert any("Payload(name='x'" in message for message in captured_logs)


async def test_plain_object_arguments_render_public_attributes_only(captured_logs):
    echo(Record())

    joined = "\n".join(captured_logs)
    assert "public='shown'" in joined
    assert "_private" not in joined


async def test_self_is_skipped_for_instance_methods(captured_logs):
    assert Service().method(5) == 5

    assert any("params: (value=5)" in message for message in captured_logs)


async def test_cls_is_skipped_for_class_methods(captured_logs):
    assert Service.class_method(7) == 7

    assert any("params: (value=7)" in message for message in captured_logs)


async def test_function_without_arguments_logs_without_params(captured_logs):
    @try_catch_log
    def no_args():
        return "ok"

    captured_logs.clear()
    assert no_args() == "ok"
    assert any("Start to call no_args." in message for message in captured_logs)


async def test_omitted_optional_positional_argument_does_not_crash(captured_logs):
    @try_catch_log
    def with_optional(first, second=None):
        return (first, second)

    assert with_optional("a") == ("a", None)
    assert any("params: (first='a')" in message for message in captured_logs)
