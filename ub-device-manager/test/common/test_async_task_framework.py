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
"""Unit tests for :mod:`ub_device_manager.common.async_task_framework`."""
import pytest

from ub_device_manager.common import async_task_framework as framework
from ub_device_manager.common.async_task_framework import (
    AsyncTask,
    AsyncTaskChain,
    BaseExceptionHandler,
    Context,
    acquire_lock,
    release_lock,
)


@pytest.fixture(autouse=True)
def clean_global_state():
    framework._LOCKS.clear()
    yield
    framework._LOCKS.clear()


class RecorderTask(AsyncTask):
    def __init__(self, name, trace, result=None, fail=False, run=True):
        self.name = name
        self.trace = trace
        self.result = result
        self.fail = fail
        self._run = run

    async def should_run(self):
        return self._run

    async def execute(self):
        self.trace.append(self.name)
        self.context.set(self.name, self.result)
        if self.fail:
            raise RuntimeError(f"{self.name} failed")
        return self.result


class TestContext:
    async def test_get_returns_default_dict_when_missing(self):
        assert Context().get("missing") == {}

    async def test_get_returns_provided_default(self):
        assert Context().get("missing", 5) == 5

    async def test_set_get_clear(self):
        context = Context()
        context.set("key", "value")
        assert context.get("key") == "value"

        context.clear("key")
        assert context.get("key") == {}

    async def test_clear_missing_key_is_a_noop(self):
        context = Context()
        context.clear("missing")
        assert context.to_dict() == {}

    async def test_initial_data_is_copied(self):
        source = {"a": 1}
        context = Context(source)
        context.set("b", 2)

        assert source == {"a": 1}
        assert context.to_dict() == {"a": 1, "b": 2}

    async def test_clear_all_and_str(self):
        context = Context({"a": 1})
        assert str(context) == "Context({'a': 1})"

        context.clear_all()
        assert context.to_dict() == {}


class TestLockHelpers:
    async def test_acquire_and_release_round_trip(self):
        assert await acquire_lock("lock-a") is True
        await release_lock("lock-a")

    async def test_acquire_is_reentrant_within_the_same_context(self):
        assert await acquire_lock("lock-a") is True
        assert await acquire_lock("lock-a") is True
        await release_lock("lock-a")

    async def test_acquire_times_out_for_another_context(self):
        assert await acquire_lock("lock-a") is True
        token = framework._current_context.set(Context())
        try:
            assert await acquire_lock("lock-a", timeout=0.01) is False
        finally:
            framework._current_context.reset(token)
        await release_lock("lock-a")

    async def test_release_without_holding_is_a_noop(self):
        await release_lock("lock-never-held")
        assert "lock-never-held" not in framework._LOCKS or not framework._LOCKS["lock-never-held"].locked()


class TestAsyncTask:
    async def test_default_should_run_is_true(self):
        class SimpleTask(AsyncTask):
            async def execute(self):
                return None

        assert await SimpleTask().should_run() is True

    async def test_execute_is_abstract(self):
        with pytest.raises(TypeError):
            AsyncTask()

    async def test_when_raise_exception_defaults_to_reraise(self):
        class FailingTask(AsyncTask):
            async def execute(self):
                return None

        with pytest.raises(ValueError):
            await FailingTask().when_raise_exception(ValueError("boom"))


class TestAsyncTaskChain:
    async def test_empty_chain_raises(self):
        with pytest.raises(ValueError, match="cannot be empty"):
            await AsyncTaskChain().run_chain()

    async def test_tasks_run_in_order_and_share_context(self):
        trace = []
        chain = AsyncTaskChain().with_context({"seed": 1})
        chain._task_steps = [
            lambda: RecorderTask("first", trace, result=1),
            lambda: RecorderTask("second", trace, result=2),
        ]

        context = await chain.run_chain()

        assert trace == ["first", "second"]
        assert context.get("first") == 1
        assert context.get("second") == 2

    async def test_condition_false_skips_task(self):
        trace = []
        chain = AsyncTaskChain()
        chain.apply_async_task(RecorderTask, condition=lambda _: False)

        assert chain._task_steps == []
        assert trace == []

    async def test_apply_async_task_rejects_non_task_classes(self):
        with pytest.raises(TypeError, match="must inherit AsyncTask"):
            AsyncTaskChain().apply_async_task(dict)

    async def test_should_run_false_skips_execution(self):
        trace = []
        chain = AsyncTaskChain()
        chain._task_steps = [lambda: RecorderTask("skipped", trace, run=False)]

        context = await chain.run_chain()

        assert trace == []
        assert context.get("skipped") == {}

    async def test_when_raise_exception_can_swallow_error(self):
        class SwallowingTask(AsyncTask):
            async def execute(self):
                raise ValueError("ignored")

            async def when_raise_exception(self, exception):
                self.context.set("handled", str(exception))

        chain = AsyncTaskChain()
        chain._task_steps = [SwallowingTask]

        context = await chain.run_chain()

        assert context.get("handled") == "ignored"

    async def test_global_exception_handler_is_called_and_error_reraised(self):
        handled = {}

        class Handler(BaseExceptionHandler):
            async def handle_exception(self, exception, context):
                handled["error"] = str(exception)

        chain = AsyncTaskChain().set_global_exception_handler(Handler)
        chain._task_steps = [lambda: RecorderTask("failing", [], fail=True)]

        with pytest.raises(RuntimeError, match="failing failed"):
            await chain.run_chain()

        assert handled["error"] == "failing failed"

    async def test_set_global_exception_handler_validates_type(self):
        with pytest.raises(TypeError, match="must inherit BaseExceptionHandler"):
            AsyncTaskChain().set_global_exception_handler(object)

    async def test_chain_releases_locks_it_acquired(self):
        chain = AsyncTaskChain()
        chain.apply_acquire_lock("chain-lock")

        await chain.run_chain()

        assert not framework._LOCKS["chain-lock"].locked()

    async def test_apply_release_lock_and_acquire_timeout_task(self):
        chain = AsyncTaskChain()
        chain.apply_acquire_lock("timeout-lock", timeout=0.01)

        await chain.run_chain()

    async def test_acquire_lock_task_raises_on_timeout(self):
        await acquire_lock("held-lock")
        token = framework._current_context.set(Context())
        try:
            chain = AsyncTaskChain()
            chain.apply_acquire_lock("held-lock", timeout=0.01)
            with pytest.raises(RuntimeError, match="timeout"):
                await chain.run_chain()
        finally:
            framework._current_context.reset(token)
