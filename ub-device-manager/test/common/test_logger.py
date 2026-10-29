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
"""Unit tests for :mod:`ub_device_manager.common.logger`."""
from unittest.mock import MagicMock, patch

import pytest

from ub_device_manager.common import logger as log_module


class TestSanitizeLogMessage:
    async def test_control_characters_are_removed(self):
        assert log_module.sanitize_log_message("a\nb\r\tc\x00") == "abc"

    async def test_non_string_input_is_converted(self):
        assert log_module.sanitize_log_message(123) == "123"

    async def test_plain_text_is_unchanged(self):
        assert log_module.sanitize_log_message("hello world") == "hello world"

    async def test_unicode_c1_control_characters_are_removed(self):
        assert log_module.sanitize_log_message("x\x85y") == "xy"


class TestRequestIdFilter:
    def _record(self, message="hello", name="app", function="run"):
        return {"extra": {}, "message": message, "name": name, "function": function}

    async def test_missing_request_id_is_rendered_as_dash(self):
        token = log_module.REQUEST_ID_VAR.set("")
        try:
            record = self._record()
            assert log_module.request_id_filter(record) is True
            assert record["extra"]["request_id"] == "-"
        finally:
            log_module.REQUEST_ID_VAR.reset(token)

    async def test_present_request_id_is_propagated(self):
        token = log_module.REQUEST_ID_VAR.set("req-123")
        try:
            record = self._record()
            log_module.request_id_filter(record)
            assert record["extra"]["request_id"] == "req-123"
        finally:
            log_module.REQUEST_ID_VAR.reset(token)

    async def test_message_is_sanitized(self):
        record = self._record(message="line1\nFAKE LOG")
        log_module.request_id_filter(record)

        assert record["message"] == "line1FAKE LOG"

    async def test_logging_callhandlers_records_are_dropped(self):
        record = self._record(name="logging", function="callHandlers")

        assert log_module.request_id_filter(record) is False


class TestSetupLogging:
    @patch.object(
        log_module,
        "CONFIG",
        {"log": {"level": "DEBUG", "stdout": True, "max_file_size": "1 MB", "max_file_count": 3}},
    )
    @patch.object(log_module, "logger")
    async def test_setup_logging_configures_sinks_and_intercepts_uvicorn(self, mock_logger):
        log_module.setup_logging()

        mock_logger.remove.assert_called_once()
        assert mock_logger.add.call_count == 2
        first_call = mock_logger.add.call_args_list[0]
        assert first_call.kwargs["sink"] == log_module.LOG_PATH
        assert first_call.kwargs["level"] == "DEBUG"

    @patch.object(log_module, "CONFIG", {"log": {"stdout": False}})
    @patch.object(log_module, "logger")
    async def test_setup_logging_without_stdout_only_adds_the_file_sink(self, mock_logger):
        log_module.setup_logging()

        assert mock_logger.add.call_count == 1


class TestInterceptHandler:
    @patch.object(log_module, "logger")
    async def test_emit_forwards_to_loguru(self, mock_logger):
        record = MagicMock()
        record.levelname = "INFO"
        record.levelno = 20
        record.getMessage.return_value = "hello"
        record.exc_info = None

        log_module.InterceptHandler().emit(record)

        mock_logger.level.assert_called_once_with("INFO")
        mock_logger.opt.return_value.log.assert_called_once()

    @patch.object(log_module, "logger")
    async def test_emit_falls_back_to_numeric_level(self, mock_logger):
        mock_logger.level.side_effect = ValueError("unknown")
        record = MagicMock()
        record.levelname = "CUSTOM"
        record.levelno = 42
        record.getMessage.return_value = "hello"
        record.exc_info = None

        log_module.InterceptHandler().emit(record)

        mock_logger.opt.return_value.log.assert_called_once_with(42, "hello")


@pytest.mark.parametrize("value", ["a\nb", "a\rb", "a\x00b", "a\x1fb"])
async def test_sanitize_log_message_removes_each_control_character(value):
    assert "\n" not in log_module.sanitize_log_message(value)
