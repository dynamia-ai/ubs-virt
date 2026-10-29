##########################################################################################################
# Copyright (c) Huawei Technologies Co., Ltd. 2026. All rights reserved.
#
# ub-device-manager is licensed under the Mulan PSL v2.
# You can use this software according to the terms and conditions of the Mulan PSL v2.
# You may obtain a copy of Mulan PSL v2 at:
#     http://license.coscl.org.cn/MulanPSL2
# THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR
# IMPLIED, INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY OR FIT FOR A PARTICULAR PURPOSE.
# See the Mulan PSL v2 for more details.
##########################################################################################################
import os
from typing import Any, Optional

import httpx


DEFAULT_UDS_PATH = "/var/run/ub_device_manager/ub_device_manager.sock"


class UbDeviceManagerClient:
    def __init__(
        self,
        base_url: Optional[str] = None,
        uds_path: Optional[str] = None,
        timeout: float = 10.0,
    ):
        self.timeout = timeout
        self.uds_path = uds_path or os.getenv("UBDM_UDS_PATH", DEFAULT_UDS_PATH)
        self.base_url = base_url or "http://ub-device-manager"

    def _build_client(self) -> httpx.Client:
        transport = httpx.HTTPTransport(uds=self.uds_path)
        return httpx.Client(
            base_url=self.base_url,
            timeout=self.timeout,
            transport=transport,
            trust_env=False,
        )

    def request(self, method: str, path: str, **kwargs: Any) -> Any:
        with self._build_client() as client:
            response = client.request(method, path, **kwargs)
            response.raise_for_status()
            if not response.content:
                return None
            return response.json()

    def post(self, path: str, json: Optional[dict[str, Any]] = None) -> Any:
        return self.request("POST", path, json=json)
