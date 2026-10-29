# -*- coding: utf-8 -*-
# Copyright (c) Huawei Technologies Co., Ltd. 2025-2025. All rights reserved.
import asyncio
from contextvars import ContextVar
from abc import ABC, abstractmethod
from typing import Dict, Any, Generic, TypeVar, Type, List, Optional, Callable

_LOCKS: Dict[str, asyncio.Lock] = {}
_LOCK_NAMES_KEY = "_acquired_lock_names"


class Context:
    def __init__(self, initial_data: Dict[str, Any] = None):
        self._data = initial_data.copy() if initial_data else {}

    def get(self, name: str, default=None) -> Any:
        if default is None:
            default = {}
        return self._data.get(name, default)

    def set(self, key: str, value: Any) -> None:
        self._data[key] = value

    def clear(self, key: str) -> None:
        if key in self._data:
            del self._data[key]

    def clear_all(self) -> None:
        self._data.clear()

    def to_dict(self) -> Dict[str, Any]:
        return self._data.copy()

    def __str__(self) -> str:
        return f"Context({self._data})"


_current_context: ContextVar[Context] = ContextVar("current_context")


def _get_or_create_context() -> Context:
    try:
        return _current_context.get()
    except LookupError:
        default_ctx = Context()
        _current_context.set(default_ctx)
        return default_ctx


async def acquire_lock(lock_name: str = "_default_lock", timeout: Optional[float] = None) -> bool:
    """
    Acquire a named lock while a task chain is running (inside run_chain).

    Once acquired, the lock name is stored in the current task chain Context and
    released by run_chain when the chain ends, whether it succeeds or fails.
    It is reentrant within a chain: acquiring a lock already held by the current
    chain returns True without deadlocking. Outside a task chain, callers must
    release the lock themselves. Returns False instead of raising on timeout.
    """
    lock = _LOCKS.setdefault(lock_name, asyncio.Lock())
    context = _get_or_create_context()
    acquired_names = context.get(_LOCK_NAMES_KEY,)

    if not isinstance(acquired_names, set):
        acquired_names = set()

        context.set(_LOCK_NAMES_KEY, acquired_names)
    if lock_name in acquired_names:
        return True
    try:
        if timeout is not None:
            await asyncio.wait_for(lock.acquire(), timeout=timeout)
        else:
            await lock.acquire()
    except asyncio.TimeoutError:
        return False

    acquired_names.add(lock_name)
    return True


async def release_lock(lock_name: str = "_default_lock") -> None:
    """
    Release a named lock.

    Only releases a lock held by the current task chain. Calling it when the lock
    is not held by this chain, or is already unlocked, has no effect.
    """
    context = _get_or_create_context()
    acquired_names = context.get(_LOCK_NAMES_KEY)
    if not acquired_names or lock_name not in acquired_names:
        return
    lock = _LOCKS.get(lock_name)
    if lock is not None and lock.locked():
        lock.release()
    acquired_names.discard(lock_name)


def _release_chain_locks(context: Context) -> None:
    """Release all locks still held by a task chain when it ends."""
    acquired_names = context.get(_LOCK_NAMES_KEY)
    if not acquired_names:
        return
    for lock_name in list(acquired_names):
        lock = _LOCKS.get(lock_name)
        if lock is not None and lock.locked():
            lock.release()
    acquired_names.clear()


class AsyncTask(ABC):
    @property
    def context(self) -> Context:
        return _get_or_create_context()

    async def should_run(self) -> bool:
        return True

    @abstractmethod
    async def execute(self) -> None:
        raise NotImplementedError("Subclasses must implement execute method!")

    async def when_raise_exception(self, exception: Exception) -> None:
        raise exception


class _AcquireLockTask(AsyncTask):

    def __init__(self, lock_name: str, timeout: Optional[float] = None):
        self._lock_name = lock_name
        self._timeout = timeout

    async def execute(self) -> Any:
        if not await acquire_lock(self._lock_name, self._timeout):
            raise RuntimeError(f"Acquire lock '{self._lock_name}' timeout!")


class _ReleaseLockTask(AsyncTask):

    def __init__(self, lock_name: str):
        self._lock_name = lock_name

    async def execute(self) -> Any:
        await release_lock(self._lock_name)


class BaseExceptionHandler(ABC):
    @abstractmethod
    async def handle_exception(self, exception: Exception, context: Context) -> None:
        raise NotImplementedError("Subclasses must implement handle_exception method!")


class AsyncTaskChain:
    def __init__(self):
        self._task_steps: List[Callable[[], AsyncTask]] = []
        self._context: Context = Context()
        self._global_exception_handler: Optional[BaseExceptionHandler] = None

    def with_context(self, initial_data: Dict[str, Any]) -> "AsyncTaskChain":
        self._context = Context(initial_data)
        return self

    def set_global_exception_handler(self,
                                     handler_cls: Type[BaseExceptionHandler]) -> "AsyncTaskChain":
        if not issubclass(handler_cls, BaseExceptionHandler):
            raise TypeError(f"Handler {handler_cls.__name__} must inherit BaseExceptionHandler!")
        self._global_exception_handler = handler_cls()
        return self

    def apply_async_task(
        self,
        task_cls: Type[AsyncTask],
        condition: Callable[[Context], bool] = lambda _: True,
    ) -> "AsyncTaskChain":
        if not issubclass(task_cls, AsyncTask):
            raise TypeError(f"Task {task_cls.__name__} must inherit AsyncTask!")
        if not condition(self._context):
            return self
        self._task_steps.append(lambda: task_cls())
        return self

    def apply_acquire_lock(self, lock_name: str = "_default_lock",
                           timeout: Optional[float] = None) -> "AsyncTaskChain":

        self._task_steps.append(lambda: _AcquireLockTask(lock_name, timeout))
        return self

    def apply_release_lock(self, lock_name: str = "_default_lock") -> "AsyncTaskChain":

        self._task_steps.append(lambda: _ReleaseLockTask(lock_name))
        return self

    async def _run_single_task(self, task_inst: AsyncTask) -> Any:
        try:
            if await task_inst.should_run():
                return await task_inst.execute()
        except Exception as e:
            await task_inst.when_raise_exception(e)

    async def run_chain(self) -> Context:
        if not self._task_steps:
            raise ValueError("Task chain cannot be empty, please apply at least one task!")
        token = _current_context.set(self._context)

        try:
            for task_factory in self._task_steps:
                await self._run_single_task(task_factory())
            return self._context
        except Exception as e:
            if self._global_exception_handler is not None:
                await self._global_exception_handler.handle_exception(e, self._context)
            raise
        finally:
            _release_chain_locks(self._context)
            _current_context.reset(token)
