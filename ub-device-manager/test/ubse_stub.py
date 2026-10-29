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
"""Minimal fake ``ubse`` package used by the unit tests.

UBSE is a native runtime that is unavailable on development/pipeline machines.
The production code imports it lazily inside each method, so installing this
stub lets the surrounding logic be exercised without the native binding.
Every stub function raises ``NotImplementedError`` and is expected to be
patched by the individual test through the ``ubse_modules`` fixture.
"""
import sys
import types
from enum import IntEnum


def _new_module(name):
    module = types.ModuleType(name)
    module.__path__ = []  # allow submodule imports
    sys.modules[name] = module
    return module


def _not_implemented(*args, **kwargs):
    raise NotImplementedError("ubse stub: patch this function in the test")


def install():
    """Install the fake ``ubse`` package and return it (idempotent)."""
    existing = sys.modules.get("ubse")
    if existing is not None and getattr(existing, "_is_stub", False):
        return existing

    ubse = types.ModuleType("ubse")
    ubse.__path__ = []
    ubse._is_stub = True
    sys.modules["ubse"] = ubse

    models = _new_module("ubse.models")
    ubse.models = models

    # ---- ubse.ubs_engine_log -------------------------------------------------
    log_mod = _new_module("ubse.ubs_engine_log")
    log_mod.ubs_engine_log_callback_register = lambda callback: None
    ubse.ubs_engine_log = log_mod

    # ---- ubse.ubs_engine_npu -------------------------------------------------
    npu_mod = _new_module("ubse.ubs_engine_npu")
    for name in ("get_host_ub_devices", "alloc_devices", "query_uba_tid_size", "free_devices"):
        setattr(npu_mod, name, _not_implemented)
    ubse.ubs_engine_npu = npu_mod

    # ---- ubse.ubs_engine_ssu -------------------------------------------------
    ssu_mod = _new_module("ubse.ubs_engine_ssu")
    for name in (
        "ubs_ssu_connect_info_get",
        "ubs_ssu_ns_stats_get",
        "ubs_ssu_fe_device_alloc",
        "ubs_ssu_fe_device_list",
        "ubs_ssu_space_alloc",
        "ubs_ssu_space_free",
        "ubs_ssu_alloc_info_list",
        "ubs_ssu_access_permission_add",
        "ubs_ssu_access_permission_remove",
        "ubs_ssu_fe_device_free",
        "ubs_ssu_space_attach",
        "ubs_ssu_space_detach",
    ):
        setattr(ssu_mod, name, _not_implemented)
    ubse.ubs_engine_ssu = ssu_mod

    # ---- ubse.models.ubs_engine_model_ssu ------------------------------------
    ssu_models = _new_module("ubse.models.ubs_engine_model_ssu")

    class _AttributeContainer:
        def __init__(self, **kwargs):
            self.__dict__.update(kwargs)

    class UbsSsuAllocStrategy(IntEnum):
        STRIPED = 0
        LINEAR = 1
        NORMAL = 2

    class UbsSsuLbaFormat(IntEnum):
        FORMAT_512 = 512
        FORMAT_4096 = 4096

    ssu_models.UbsUbVfe = _AttributeContainer
    ssu_models.UbsSsuAllocSpaceReq = _AttributeContainer
    ssu_models.UbsSsuSpaceReq = _AttributeContainer
    ssu_models.UbsSsuAllocStrategy = UbsSsuAllocStrategy
    ssu_models.UbsSsuLbaFormat = UbsSsuLbaFormat
    models.ubs_engine_model_ssu = ssu_models

    return ubse
