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
"""Unit tests for :mod:`ub_device_manager.ub_device_manager_cli`."""
from unittest.mock import MagicMock, patch

import httpx
import pytest
import typer
from rich.tree import Tree
from typer.testing import CliRunner

from ub_device_manager import ub_device_manager_cli as cli


class TestSanitizeConsoleText:
    async def test_ansi_escape_sequences_are_removed(self):
        assert cli.sanitize_console_text("\x1b[31mred\x1b[0m") == "red"

    async def test_osc_escape_sequences_are_removed(self):
        assert cli.sanitize_console_text("\x1b]0;title\x07text") == "text"

    async def test_control_characters_are_removed_for_single_line(self):
        assert cli.sanitize_console_text("a\nb\rc") == "abc"

    async def test_multiline_keeps_newlines(self):
        assert cli.sanitize_console_text("a\nb", multiline=True) == "a\nb"

    async def test_markup_is_escaped(self):
        assert "\\[" in cli.sanitize_console_text("[bold]x[/bold]")

    async def test_non_string_is_converted(self):
        assert cli.sanitize_console_text(42) == "42"


class TestRenderTreeData:
    @patch.object(cli, "console")
    async def test_renders_nested_structure(self, mock_console):
        cli.render_tree_data({"a": [1, {"b": 2}], "c": []})

        mock_console.print.assert_called_once()

    @patch.object(cli, "console")
    async def test_renders_with_existing_parent(self, mock_console):
        cli.render_tree_data([1, 2], parent_tree=Tree("root"))

        mock_console.print.assert_not_called()


class TestResolveSchema:
    async def test_resolves_ref(self):
        components = {"schemas": {"Thing": {"type": "string"}}}

        assert cli.resolve_schema({"$ref": "#/components/schemas/Thing"}, components) == {"type": "string"}

    async def test_passthrough_for_plain_schema(self):
        assert cli.resolve_schema({"type": "integer"}, {}) == {"type": "integer"}

    async def test_none_is_returned_as_is(self):
        assert cli.resolve_schema(None, {}) is None


class TestTryParseJson:
    @pytest.mark.parametrize(
        ("raw", "expected"),
        [
            ("'abc'", "'abc'"),
            ('"abc"', '"abc"'),
            ("123", 123),
            ("1.5", 1.5),
            ("true", True),
            ("[1,2]", [1, 2]),
            ('{"a": 1}', {"a": 1}),
            ("abc", "abc"),
        ],
    )
    async def test_parses_common_inputs(self, raw, expected):
        assert cli.try_parse_json(raw) == expected

    async def test_non_string_is_returned_unchanged(self):
        assert cli.try_parse_json(5) == 5


class TestParseRelaxedJson:
    async def test_parses_unquoted_keys(self):
        assert cli.parse_relaxed_json("{a:1,b:2}") == {"a": 1, "b": 2}

    async def test_parses_quoted_values(self):
        assert cli.parse_relaxed_json('{"a":"x y","b":2}') == {"a": "x y", "b": 2}

    async def test_empty_input_returns_none(self):
        assert cli.parse_relaxed_json("   ") is None

    async def test_invalid_input_returns_none(self):
        assert cli.parse_relaxed_json("not json at all") is None


class TestParseKeyValuePairs:
    async def test_comma_separated(self):
        assert cli.parse_key_value_pairs("a=1,b=2") == {"a": 1, "b": 2}

    async def test_space_separated(self):
        assert cli.parse_key_value_pairs("a=1 b=2") == {"a": 1, "b": 2}

    async def test_multiword_value_is_joined(self):
        assert cli.parse_key_value_pairs("a=hello world,b=2") == {"a": "hello world", "b": 2}

    async def test_without_equals_returns_none(self):
        assert cli.parse_key_value_pairs("no-equals-here") is None


class TestTypeHelpers:
    @pytest.mark.parametrize(
        ("type_str", "expected"),
        [
            ("integer", int),
            ("number", float),
            ("boolean", bool),
            ("array", list),
            ("object", dict),
            ("string", str),
            ("unknown", str),
        ],
    )
    async def test_get_param_python_type(self, type_str, expected):
        assert cli.get_param_python_type(type_str) is expected

    async def test_get_param_cli_type_is_always_str(self):
        assert cli.get_param_cli_type("integer") is str


class TestCastAndValidateValue:
    @pytest.fixture(autouse=True)
    def quiet_console(self):
        with patch.object(cli, "console"):
            yield

    async def test_none_is_returned(self):
        assert cli.cast_and_validate_value(None, int, "p") is None

    @pytest.mark.parametrize(
        ("raw", "expected"),
        [("yes", True), ("no", False), ("true", True), ("0", False), (True, True), (1, True)],
    )
    async def test_bool_casting(self, raw, expected):
        assert cli.cast_and_validate_value(raw, bool, "p") is expected

    async def test_invalid_bool_exits(self):
        with pytest.raises(typer.Exit):
            cli.cast_and_validate_value("maybe", bool, "p")

    @pytest.mark.parametrize(
        ("raw", "expected"),
        [
            ("1,2,3", [1, 2, 3]),
            ("[1,2]", [1, 2]),
            ("'a','b'", ["'a'", "'b'"]),
            (["x"], ["x"]),
        ],
    )
    async def test_list_casting(self, raw, expected):
        assert cli.cast_and_validate_value(raw, list, "p") == expected

    async def test_invalid_list_exits(self):
        with pytest.raises(typer.Exit):
            cli.cast_and_validate_value("[unterminated", list, "p")

    @pytest.mark.parametrize(
        ("raw", "expected"),
        [
            ("a=1,b=2", {"a": 1, "b": 2}),
            ('{"a": 1}', {"a": 1}),
            ({"x": 1}, {"x": 1}),
        ],
    )
    async def test_dict_casting(self, raw, expected):
        assert cli.cast_and_validate_value(raw, dict, "p") == expected

    async def test_invalid_dict_exits(self):
        with pytest.raises(typer.Exit):
            cli.cast_and_validate_value("{unterminated", dict, "p")

    async def test_scalar_casting(self):
        assert cli.cast_and_validate_value("42", int, "p") == 42

    async def test_invalid_scalar_exits(self):
        with pytest.raises(typer.Exit):
            cli.cast_and_validate_value("not-a-number", int, "p")


class TestParseRouteToCmd:
    @pytest.mark.parametrize(
        ("path", "method", "expected"),
        [
            ("/npu", "get", ("npu", "list")),
            ("/npu", "post", ("npu", "create")),
            ("/npu/bind", "post", ("npu", "bind")),
            ("/vm/{name}", "get", ("vm", "show")),
            ("/vm/{name}", "delete", ("vm", "delete")),
            ("/vm/{name}", "put", ("vm", "update")),
            ("/ssu/{name}", "patch", ("ssu", "update")),
            ("/", "get", (None, None)),
        ],
    )
    async def test_route_mapping(self, path, method, expected):
        assert cli.parse_route_to_cmd(path, method) == expected


class TestServerClient:
    async def test_get_server_client_configuration(self):
        client = cli.get_server_client()
        try:
            assert isinstance(client, httpx.Client)
            assert str(client.base_url) == "http://ub-device-manager"
        finally:
            client.close()


class TestApplication:
    async def test_help_lists_available_groups(self):
        result = CliRunner().invoke(cli.app, ["--help"])

        assert result.exit_code == 0
        assert "npu" in result.output


async def test_openapi_spec_is_loaded():
    assert isinstance(cli.openapi_spec, dict)
    assert "paths" in cli.openapi_spec
