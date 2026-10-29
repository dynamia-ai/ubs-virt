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
"""Unit tests for :mod:`ub_device_manager.ub_dm_sdk.vm`."""
from unittest.mock import patch

from ub_device_manager.ub_dm_sdk.http_client import UbDeviceManagerClient
from ub_device_manager.ub_dm_sdk.vm import create_vm, delete_vm


class TestCreateVm:
    @patch.object(UbDeviceManagerClient, "post")
    async def test_posts_expected_payload(self, mock_post):
        mock_post.return_value = {"msg": "ok"}

        result = create_vm(upi="0x1", xml_text="<domain/>", need_nic=True, npu_count=2)

        assert result == {"msg": "ok"}
        mock_post.assert_called_once_with(
            "/vm/create",
            json={"xml_text": "<domain/>", "upi": "0x1", "need_nic": True, "npu_count": 2},
        )

    @patch.object(UbDeviceManagerClient, "post")
    async def test_excludes_none_fields(self, mock_post):
        mock_post.return_value = {"msg": "ok"}

        create_vm(upi="0x1", xml_text="<domain/>")

        assert mock_post.call_args.kwargs["json"] == {"xml_text": "<domain/>", "upi": "0x1", "need_nic": False}


class TestDeleteVm:
    @patch.object(UbDeviceManagerClient, "request", return_value={"msg": "ok"})
    async def test_deletes_by_name(self, mock_request):
        result = delete_vm("vm-1")

        assert result == {"msg": "ok"}
        mock_request.assert_called_once_with("DELETE", "/vm/vm-1")
