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
"""Unit tests for :mod:`ub_device_manager.run`."""
from unittest.mock import patch

import pytest

from ub_device_manager import run as run_module


class TestEnsureSocketDir:
    @patch.object(run_module, "SOCKET_PATH", "/tmp/x.sock")
    @patch.object(run_module.os.path, "isdir", lambda path: True)
    @patch.object(run_module.os, "makedirs")
    async def test_existing_directory_is_left_untouched(self, mock_makedirs):
        run_module.ensure_socket_dir()

        mock_makedirs.assert_not_called()

    async def test_missing_directory_is_created(self, tmp_path):
        socket_path = tmp_path / "sub" / "x.sock"
        with patch.object(run_module, "SOCKET_PATH", str(socket_path)), \
                patch.object(run_module.os, "makedirs") as mock_makedirs:
            run_module.ensure_socket_dir()

        mock_makedirs.assert_called_once_with(str(tmp_path / "sub"), exist_ok=True)

    @patch.object(run_module, "SOCKET_PATH", "/tmp/sub/x.sock")
    @patch.object(run_module.os, "makedirs", side_effect=OSError("denied"))
    async def test_makedirs_failure_exits(self, mock_makedirs):
        with pytest.raises(SystemExit):
            run_module.ensure_socket_dir()

    @patch.object(run_module, "SOCKET_PATH", "x.sock")
    @patch.object(run_module.os, "makedirs")
    async def test_relative_socket_path_without_directory_is_ignored(self, mock_makedirs):
        run_module.ensure_socket_dir()

        mock_makedirs.assert_not_called()


class TestCleanupSocket:
    async def test_existing_socket_is_removed(self, tmp_path):
        socket_path = tmp_path / "x.sock"
        socket_path.write_text("", encoding="utf-8")

        with patch.object(run_module, "SOCKET_PATH", str(socket_path)):
            run_module.cleanup_socket()

        assert not socket_path.exists()

    async def test_missing_socket_is_noop(self, tmp_path):
        with patch.object(run_module, "SOCKET_PATH", str(tmp_path / "absent.sock")):
            run_module.cleanup_socket()

    @patch.object(run_module.os, "remove", side_effect=OSError("denied"))
    async def test_remove_failure_exits(self, mock_remove, tmp_path):
        socket_path = tmp_path / "x.sock"
        socket_path.write_text("", encoding="utf-8")

        with patch.object(run_module, "SOCKET_PATH", str(socket_path)):
            with pytest.raises(SystemExit):
                run_module.cleanup_socket()


class TestSetSocketPermissions:
    @patch.object(run_module.os, "chmod")
    async def test_permissions_are_applied(self, mock_chmod):
        run_module.set_socket_permissions("/tmp/x.sock")

        mock_chmod.assert_called_once_with("/tmp/x.sock", 0o660)

    @patch.object(run_module.os, "chmod", side_effect=OSError("denied"))
    async def test_failure_is_logged_but_not_raised(self, mock_chmod):
        run_module.set_socket_permissions("/tmp/x.sock")


class TestMain:
    async def test_main_wires_uvicorn_and_cleans_up(self):
        calls = []
        fake_server = {}

        class FakeServer:
            def __init__(self, config):
                self.config = config
                fake_server["server"] = self

            async def startup(self):
                pass

            def run(self):
                calls.append("run")

        def fake_server_factory(config):
            return FakeServer(config)

        with patch.object(run_module, "setup_logging", lambda: calls.append("setup_logging")), \
                patch.object(run_module, "ensure_socket_dir", lambda: calls.append("ensure_socket_dir")), \
                patch.object(run_module, "cleanup_socket", lambda: calls.append("cleanup_socket")), \
                patch.object(run_module, "set_socket_permissions", lambda path: calls.append(("chmod", path))), \
                patch.object(run_module.uvicorn, "Config", lambda **kwargs: kwargs), \
                patch.object(run_module.uvicorn, "Server", fake_server_factory):
            run_module.main()
            await fake_server["server"].startup()

        assert "setup_logging" in calls
        assert "ensure_socket_dir" in calls
        assert "run" in calls
        # cleanup runs once before startup and once in the finally block.
        assert calls.count("cleanup_socket") == 2
        assert ("chmod", run_module.SOCKET_PATH) in calls
