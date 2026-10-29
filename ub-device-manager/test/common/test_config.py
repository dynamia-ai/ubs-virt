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
"""Unit tests for :mod:`ub_device_manager.common.config`."""
import os
from unittest.mock import patch

import pytest

from ub_device_manager.common import config


class TestResolveConfigPath:
    @patch.dict(os.environ, {config.CONFIG_PATH_ENV_VAR: "/tmp/custom.toml"})
    @patch.object(config.os.path, "isfile")
    async def test_env_var_wins_when_the_file_exists(self, mock_isfile):
        mock_isfile.side_effect = lambda path: path == "/tmp/custom.toml"

        assert config.resolve_config_path() == "/tmp/custom.toml"

    @patch.dict(os.environ, {}, clear=True)
    @patch.object(config.os.path, "isfile")
    async def test_system_path_used_when_env_var_is_absent(self, mock_isfile):
        mock_isfile.side_effect = lambda path: path == config.SYSTEM_CONFIG_PATH

        assert config.resolve_config_path() == config.SYSTEM_CONFIG_PATH

    @patch.dict(os.environ, {}, clear=True)
    @patch.object(config.os.path, "isfile", lambda path: False)
    async def test_packaged_path_is_the_fallback(self):
        assert config.resolve_config_path() == config.PACKAGED_CONFIG_PATH

    @patch.dict(os.environ, {config.CONFIG_PATH_ENV_VAR: "/tmp/missing.toml"})
    @patch.object(config.os.path, "isfile")
    async def test_missing_env_var_file_does_not_short_circuit(self, mock_isfile):
        mock_isfile.side_effect = lambda path: path == config.SYSTEM_CONFIG_PATH

        assert config.resolve_config_path() == config.SYSTEM_CONFIG_PATH


class TestParseSize:
    @pytest.mark.parametrize(
        ("value", "expected"),
        [
            (0, 0),
            (1024, 1024),
            ("1", 1),
            ("1024", 1024),
            ("1 B", 1),
            ("1 KB", 1024),
            ("1 kb", 1024),
            ("2 MB", 2 * 1024 * 1024),
            ("1 GB", 1024 * 1024 * 1024),
            ("  4MB ", 4 * 1024 * 1024),
        ],
    )
    async def test_valid_values_are_converted_to_bytes(self, value, expected):
        assert config.parse_size(value) == expected

    @pytest.mark.parametrize("value", ["", "abc", "1 TB", "-1 MB", None, [], {}])
    async def test_invalid_values_fall_back_to_the_default(self, value):
        assert config.parse_size(value, default=123) == 123

    async def test_bool_is_not_treated_as_an_integer(self):
        assert config.parse_size(True, default=42) == 42
        assert config.parse_size(False, default=42) == 42

    async def test_default_is_used_for_invalid_input(self):
        assert config.parse_size("nonsense") == config.DEFAULT_MAX_XML_SIZE


async def test_load_settings_reads_the_resolved_toml_file(tmp_path):
    toml_file = tmp_path / "settings.toml"
    toml_file.write_text('[log]\nlevel = "DEBUG"\n', encoding="utf-8")

    with patch.object(config, "resolve_config_path", return_value=str(toml_file)):
        assert config.load_settings() == {"log": {"level": "DEBUG"}}


async def test_module_level_config_is_a_dict():
    assert isinstance(config.CONFIG, dict)
