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
"""Shared test doubles and helpers for the unit tests."""
from ub_device_manager.common.async_task_framework import AsyncTaskChain, Context


async def run_task(task, context=None):
    """Execute a single AsyncTask inside a fresh task chain context."""
    chain = AsyncTaskChain().with_context(context or {})
    chain._task_steps = [lambda: task]
    return await chain.run_chain()


def make_fake_chain(result_context=None, error=None):
    """Build a stand-in for ``AsyncTaskChain`` used by the HTTP layer tests.

    The returned class mirrors the fluent builder API and either returns a
    context pre-populated with ``result_context`` or raises ``error`` when the
    chain is run, which lets the route handlers be tested in isolation.
    """

    class FakeTaskChain:
        def with_context(self, data):
            return self

        def apply_async_task(self, *args, **kwargs):
            return self

        def apply_acquire_lock(self, *args, **kwargs):
            return self

        def apply_release_lock(self, *args, **kwargs):
            return self

        async def run_chain(self):
            if error is not None:
                raise error
            return Context(dict(result_context or {}))

    return FakeTaskChain
