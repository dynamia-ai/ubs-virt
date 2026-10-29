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
import sys

import stat
from loguru import logger
import uvicorn

from ub_device_manager.common.logger import setup_logging

SOCKET_PATH = "/var/run/ub_device_manager/ub_device_manager.sock"


def ensure_socket_dir():
    socket_dir = os.path.dirname(SOCKET_PATH)
    if not socket_dir or os.path.isdir(socket_dir):
        return
    try:
        os.makedirs(socket_dir, exist_ok=True)
        logger.info(f"Created socket directory: {socket_dir}")
    except OSError as e:
        logger.error(f"Failed to create socket directory: {e}, the service cannot start!")
        sys.exit(1)


def cleanup_socket():
    if os.path.exists(SOCKET_PATH):
        try:
            os.remove(SOCKET_PATH)
            logger.info(f"Successfully cleaned up leftover socket file: {SOCKET_PATH}")
        except OSError as e:
            logger.error(f"Failed to clean up socket file: {e}, the service may fail to start properly!")
            sys.exit(1)


def set_socket_permissions(path: str):
    try:
        os.chmod(path, stat.S_IRUSR | stat.S_IWUSR | stat.S_IRGRP | stat.S_IWGRP)
        logger.info(f"Successfully set socket file permissions to: 0660")
    except OSError as e:
        logger.warning(f"Failed to set socket permissions: {e}, please check the permissions of the running user.")


def main():
    setup_logging()
    ensure_socket_dir()
    cleanup_socket()

    logger.info(f"UDS service is starting, listening on: {SOCKET_PATH}")
    config = uvicorn.Config(
        app="ub_device_manager.common.factory:final_app",
        uds=SOCKET_PATH,
        loop="asyncio",
        log_config=None,
    )
    server = uvicorn.Server(config)

    original_startup = server.startup

    async def custom_startup(*args, **kwargs):
        await original_startup(*args, **kwargs)
        set_socket_permissions(SOCKET_PATH)

    server.startup = custom_startup

    try:
        server.run()
    finally:
        cleanup_socket()


if __name__ == "__main__":
    main()
