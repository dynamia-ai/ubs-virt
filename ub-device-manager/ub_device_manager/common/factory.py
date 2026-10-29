# -*- coding: utf-8 -*-
# Copyright (c) Huawei Technologies Co., Ltd. 2025-2025. All rights reserved.
import time

import uuid
from contextlib import asynccontextmanager
from typing import Dict

from http import HTTPStatus

from fastapi import FastAPI, Request, HTTPException
from fastapi.exceptions import RequestValidationError
from loguru import logger
from starlette.responses import JSONResponse

from ub_device_manager.app.vm_app import app as vm_app
from ub_device_manager.app.npu_app import app as npu_app
from ub_device_manager.app.ssu_app import app as ssu_app
from ub_device_manager.common.logger import REQUEST_ID_VAR

_TITLE = "Ub device manager"
_VERSION = "1.0.0"
_DESCRIPTION = ""


@asynccontextmanager
async def lifespan(app: FastAPI):
    logger.info("Global service initialization completed; Loguru is configured.")
    yield
    logger.info("Service is shutting down gracefully.")


def get_logger(request: Request):
    return request.state.request_logger


_BODY_LOCATION = "body"
_PATTERN_ERROR_TYPES = frozenset({"string_pattern_mismatch", "string_pattern_mismatch_exact"})
_VALUE_ERROR_PREFIX = "Value error, "


def _collect_body_field_hints(request: Request) -> Dict[str, str]:
    """Collect readable hints for the request-body fields declared on the matched route.

    The hints are derived from the Pydantic field metadata so that the 422 response
    stays understandable even for complex constraints (such as the UPI regular
    expression) without leaking the raw regex pattern to the caller.
    """
    route = request.scope.get("route")
    body_params = getattr(getattr(route, "dependant", None), "body_params", None) or ()
    hints: Dict[str, str] = {}
    for body_param in body_params:
        annotation = getattr(getattr(body_param, "field_info", None), "annotation", None)
        if annotation is None:
            annotation = getattr(body_param, "type_", None)
        model_fields = getattr(annotation, "model_fields", None)
        if not model_fields:
            continue
        for field_name, model_field in model_fields.items():
            description = (getattr(model_field, "description", None) or "").strip().rstrip(".")
            if not description:
                continue
            examples = getattr(model_field, "examples", None) or []
            hints[field_name] = f"{description} (e.g. {examples[0]})" if examples else description
    return hints


def _format_validation_error(error: Dict, field_hints: Dict[str, str]) -> str:
    """Render a single Pydantic validation error as a human-readable message."""
    location = [str(part) for part in error.get("loc", ()) if part != _BODY_LOCATION]
    field = ".".join(location) if location else "request"
    top_level_field = location[0] if location else ""

    if error.get("type") in _PATTERN_ERROR_TYPES:
        hint = field_hints.get(top_level_field, "the value does not match the required format")
        return f"{field}: invalid format, {hint}"

    message = str(error.get("msg", "invalid value"))
    if message.startswith(_VALUE_ERROR_PREFIX):
        message = message[len(_VALUE_ERROR_PREFIX):]
    return f"{field}: {message}"


def create_app() -> FastAPI:
    new_app = FastAPI(
        title=_TITLE,
        version=_VERSION,
        description=_DESCRIPTION,
        lifespan=lifespan
    )

    @new_app.middleware("http")
    async def add_process_time_header(request: Request, call_next):
        start_time = time.time()
        response = await call_next(request)

        process_time = time.time() - start_time
        logger.info(f"Request completed: {request.method} {request.url.path} | duration: {process_time:.4f}s")

        response.headers["X-Process-Time"] = str(process_time)
        return response

    @new_app.middleware("http")
    async def request_id_middleware(request: Request, call_next):
        req_id = str(uuid.uuid4())
        token = REQUEST_ID_VAR.set(req_id)
        try:
            logger.info(f"Start handle request: {request.method} {request.url.path}")
            response = await call_next(request)
            logger.info(f"Finish handle request, status_code={response.status_code}")

            response.headers["X-Request-ID"] = req_id

            return response
        finally:
            REQUEST_ID_VAR.reset(token)

    @new_app.exception_handler(RequestValidationError)
    async def request_validation_exception_handler(
            request: Request, exc: RequestValidationError):
        field_hints = _collect_body_field_hints(request)
        details = "; ".join(
            _format_validation_error(error, field_hints) for error in exc.errors()
        )
        error_msg = f"Invalid request parameters: {details}"
        logger.error(
            f"Request validation failed: {request.method} {request.url.path}, "
            f"detail: {error_msg}")

        return JSONResponse(
            content=error_msg,
            status_code=HTTPStatus.UNPROCESSABLE_ENTITY
        )

    @new_app.exception_handler(HTTPException)
    async def http_exception_handler(request: Request, exc: HTTPException):
        # exc.status_code is the HTTP status code and exc.detail is the error message.
        logger.error(
            f"Request failed: {request.method} {request.url.path}, "
            f"code: {exc.status_code}, detail: {exc.detail}")

        return JSONResponse(
            content=exc.detail,
            status_code=exc.status_code
        )

    @new_app.exception_handler(Exception)
    async def global_exception_handler(request: Request, exc: Exception):
        error_msg = f"Exception: {request.method} {request.url.path}, error: {exc}"
        logger.error(error_msg)

        return JSONResponse(
            content=error_msg,
            status_code=HTTPStatus.INTERNAL_SERVER_ERROR
        )

    new_app.router.routes.extend(vm_app.routes + npu_app.routes + ssu_app.routes)

    return new_app


final_app = create_app()
