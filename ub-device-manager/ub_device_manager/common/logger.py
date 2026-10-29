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
from loguru import logger
import logging
import re
import sys

from ub_device_manager.common.config import CONFIG
from contextvars import ContextVar

REQUEST_ID_VAR = ContextVar("request_id", default="")
LOG_PATH = "/var/log/ub_device_manager/ub_device_manager.log"

# Matches C0/C1 control characters (including \r, \n, etc.) to prevent log injection
CONTROL_CHAR_PATTERN = re.compile(r"[\x00-\x1f\x7f-\x9f]")


def sanitize_log_message(message):
    """Remove control characters such as newlines and carriage returns to prevent log injection."""
    if not isinstance(message, str):
        message = str(message)
    return CONTROL_CHAR_PATTERN.sub("", message)


class InterceptHandler(logging.Handler):
    def emit(self, record):
        try:
            level = logger.level(record.levelname).name
        except ValueError:
            level = record.levelno

        frame, depth = logging.currentframe(), 2
        while frame.f_code.co_filename == logging.__file__:
            frame = frame.f_back
            depth += 1

        logger.opt(depth=depth, exception=record.exc_info).log(level, record.getMessage())


def request_id_filter(record):
    req_id = REQUEST_ID_VAR.get()
    record["extra"]["request_id"] = req_id if req_id else "-"
    # Filter the message content to avoid log injection via control characters such as \r and \n
    record["message"] = sanitize_log_message(record["message"])
    if record["name"] == "logging" and record["function"] == "callHandlers":
        return False
    return True


def setup_logging():
    logging.getLogger("uvicorn.error").disabled = True
    logger.remove()

    log_config = CONFIG.get("log", {})
    log_level = log_config.get("level", "INFO")

    logger.add(
        sink=LOG_PATH,
        backtrace=False,
        diagnose=False,
        rotation=log_config.get("max_file_size", "10 MB"),
        retention=log_config.get("max_file_count", 10),
        compression="zip",
        level=log_level,
        enqueue=True,
        format="<green>{time:YYYY-MM-DD HH:mm:ss}</green> | <level>{level: <8}</level> | "
               "<magenta>{extra[request_id]}</magenta> | <blue>{name}:{function}:{line}</blue> - <level>{message}</level>"
        , filter=request_id_filter)

    if log_config.get("stdout", False):
        logger.add(
            sink=sys.stdout,
            level=log_level,
            enqueue=True,
            format="<green>{time:YYYY-MM-DD HH:mm:ss}</green> | <level>{level: <8}</level> | "
                   "<magenta>{extra[request_id]}</magenta> | <blue>{name}:{function}:{line}</blue> - <level>{message}</level>"
            , filter=request_id_filter
        )
    # 4. Intercept Uvicorn and FastAPI logs.
    # Replace handlers on common loggers with InterceptHandler.
    seen = set()
    for name in logging.root.manager.loggerDict.keys():
        if name.startswith("uvicorn") or name.startswith("fastapi"):
            _logger = logging.getLogger(name)
            _logger.handlers = [InterceptHandler()]
            seen.add(name)

    logging.getLogger().handlers = [InterceptHandler()]
