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
"""Unit tests for :mod:`ub_device_manager.ub_dm_sdk.http_client`."""
import os
from unittest.mock import patch

import pytest

from ub_device_manager.ub_dm_sdk import http_client
from ub_device_manager.ub_dm_sdk.http_client import DEFAULT_UDS_PATH, UbDeviceManagerClient


class FakeResponse:
    def __init__(self, payload=None, content=b"{}"):
        self._payload = payload
        self.content = content
        self.raise_for_status_called = False

    def raise_for_status(self):
        self.raise_for_status_called = True

    def json(self):
        return self._payload


class FakeClientContext:
    def __init__(self, response):
        self.response = response
        self.requests = []
        self.closed = False

    def __enter__(self):
        return self

    def __exit__(self, *exc_info):
        self.closed = True
        return False

    def request(self, method, path, **kwargs):
        self.requests.append((method, path, kwargs))
        return self.response


class TestDefaults:
    @patch.dict(os.environ, {}, clear=True)
    async def test_default_values(self):
        client = UbDeviceManagerClient()

        assert client.uds_path == DEFAULT_UDS_PATH
        assert client.base_url == "http://ub-device-manager"
        assert client.timeout == 10.0

    @patch.dict(os.environ, {"UBDM_UDS_PATH": "/tmp/custom.sock"})
    async def test_environment_variable_sets_uds_path(self):
        assert UbDeviceManagerClient().uds_path == "/tmp/custom.sock"

    @patch.dict(os.environ, {"UBDM_UDS_PATH": "/tmp/env.sock"})
    async def test_explicit_arguments_win(self):
        client = UbDeviceManagerClient(base_url="http://x", uds_path="/tmp/arg.sock", timeout=3)

        assert client.uds_path == "/tmp/arg.sock"
        assert client.base_url == "http://x"
        assert client.timeout == 3


class TestBuildClient:
    @patch.object(http_client.httpx, "Client")
    @patch.object(http_client.httpx, "HTTPTransport")
    async def test_build_client_forwards_configuration(self, mock_transport, mock_client):
        UbDeviceManagerClient(uds_path="/tmp/x.sock", timeout=5)._build_client()

        mock_transport.assert_called_once_with(uds="/tmp/x.sock")
        kwargs = mock_client.call_args.kwargs
        assert kwargs["base_url"] == "http://ub-device-manager"
        assert kwargs["timeout"] == 5
        assert kwargs["trust_env"] is False


class TestRequest:
    @patch.object(UbDeviceManagerClient, "_build_client")
    async def test_returns_parsed_json(self, mock_build):
        context = FakeClientContext(FakeResponse(payload={"ok": True}))
        mock_build.return_value = context

        assert UbDeviceManagerClient().request("GET", "/npu") == {"ok": True}
        assert context.requests == [("GET", "/npu", {})]
        assert context.closed is True

    @patch.object(UbDeviceManagerClient, "_build_client")
    async def test_empty_body_returns_none(self, mock_build):
        mock_build.return_value = FakeClientContext(FakeResponse(content=b""))

        assert UbDeviceManagerClient().request("DELETE", "/vm/a") is None

    @patch.object(UbDeviceManagerClient, "_build_client")
    async def test_raise_for_status_is_invoked(self, mock_build):
        response = FakeResponse(payload={})
        mock_build.return_value = FakeClientContext(response)

        UbDeviceManagerClient().request("GET", "/ssu")

        assert response.raise_for_status_called is True

    @patch.object(UbDeviceManagerClient, "_build_client")
    async def test_post_delegates_to_request(self, mock_build):
        context = FakeClientContext(FakeResponse(payload={"ok": 1}))
        mock_build.return_value = context

        assert UbDeviceManagerClient().post("/npu/bind", json={"upi": "0x1"}) == {"ok": 1}
        assert context.requests[0][0] == "POST"
        assert context.requests[0][2] == {"json": {"upi": "0x1"}}


@pytest.mark.parametrize("path", ["/npu", "/vm/create", "/ssu"])
@patch.object(UbDeviceManagerClient, "_build_client")
async def test_request_path_is_forwarded(mock_build, path):
    context = FakeClientContext(FakeResponse(payload={}))
    mock_build.return_value = context

    UbDeviceManagerClient().post(path, json={})

    assert context.requests[0][1] == path
