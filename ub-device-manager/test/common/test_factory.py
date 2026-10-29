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
"""Unit tests for :mod:`ub_device_manager.common.factory`."""
from unittest.mock import patch

import pytest
from fastapi import FastAPI
from fastapi.testclient import TestClient
from starlette.requests import Request
from test.helpers import make_fake_chain

from ub_device_manager.app import npu_app, vm_app
from ub_device_manager.common.factory import (
    _format_validation_error,
    create_app,
    get_logger,
)
from ub_device_manager.constants import NPU_LIST_CONTEXT_KEY


@pytest.fixture
def app():
    return create_app()


@pytest.fixture
def client(app):
    return TestClient(app, raise_server_exceptions=False)


class TestCreateApp:
    async def test_returns_fastapi_instance(self, app):
        assert isinstance(app, FastAPI)

    async def test_registers_all_module_routes(self, app):
        paths = {route.path for route in app.routes}

        assert {"/vm/create", "/vm/{name}", "/npu", "/npu/bind", "/npu/unbind"} <= paths
        assert {"/ssu", "/ssu/{name}", "/ssu/alloc", "/ssu-vfe"} <= paths


class TestMiddleware:
    @patch.object(npu_app, "AsyncTaskChain", make_fake_chain({NPU_LIST_CONTEXT_KEY: []}))
    async def test_request_and_timing_headers_are_added(self, client):
        response = client.get("/npu")

        assert "X-Request-ID" in response.headers
        assert "X-Process-Time" in response.headers


class TestValidationErrorHandler:
    async def test_pattern_mismatch_reports_readable_hint(self, client):
        response = client.post("/vm/create", json={"upi": "invalid", "xml_text": "<domain/>"})

        assert response.status_code == 422
        body = response.json()
        assert "Invalid request parameters" in body
        assert "upi" in body
        assert "invalid format" in body


class TestHttpExceptionHandler:
    async def test_business_exception_status_and_detail(self, client):
        response = client.post("/npu/unbind", json={"bus_guid": "  "})

        assert response.status_code == 400
        assert response.json() == "bus_guid is required"


class TestGlobalExceptionHandler:
    @patch.object(vm_app, "AsyncTaskChain", make_fake_chain(error=RuntimeError("boom")))
    async def test_unexpected_error_returns_500(self, client):
        response = client.post("/vm/create", json={"upi": "0x1", "xml_text": "<domain/>"})

        assert response.status_code == 500
        assert "boom" in response.json()


class TestFormatValidationError:
    async def test_pattern_error_uses_field_hint(self):
        error = {"type": "string_pattern_mismatch", "loc": ("body", "upi"), "msg": "ignored"}

        assert _format_validation_error(error, {"upi": "the hint"}) == "upi: invalid format, the hint"

    async def test_pattern_error_without_hint_uses_generic_message(self):
        error = {"type": "string_pattern_mismatch", "loc": ("body", "upi"), "msg": "ignored"}

        assert _format_validation_error(error, {}) == "upi: invalid format, the value does not match the required format"

    async def test_value_error_prefix_is_stripped(self):
        error = {"type": "value_error", "loc": ("body", "name"), "msg": "Value error, must be set"}

        assert _format_validation_error(error, {}) == "name: must be set"

    async def test_missing_location_falls_back_to_request(self):
        error = {"type": "value_error", "loc": (), "msg": "broken"}

        assert _format_validation_error(error, {}) == "request: broken"

    async def test_body_location_is_removed_from_field_name(self):
        error = {"type": "value_error", "loc": ("body", "items", 0, "id"), "msg": "bad"}

        assert _format_validation_error(error, {}) == "items.0.id: bad"


class TestGetLogger:
    async def test_returns_request_state_logger(self):
        request = Request({"type": "http", "headers": [], "method": "GET", "path": "/"})
        request.state.request_logger = "logger-object"

        assert get_logger(request) == "logger-object"
