import inspect

from loguru import logger
from functools import wraps


def try_catch_log(func):
    """
    Decorator that wraps a function in try/except.
    Usage: @try_catch_log
    """
    is_async = inspect.iscoroutinefunction(func)
    sig = inspect.signature(func)
    params_list = list(sig.parameters.keys())

    def format_args(args, kwargs):
        args_copy = list(args)
        kw_copy = kwargs.copy()
        bind_args = {}

        skip_num = 0
        if params_list and params_list[0] in ("self", "cls"):
            skip_num = 1

        for idx, param_name in enumerate(params_list[skip_num:]):
            if idx + skip_num < len(args_copy):
                bind_args[param_name] = args_copy[idx + skip_num]

        bind_args.update(kw_copy)

        def safe_repr(val):
            if hasattr(val, "model_dump"):
                data = val.model_dump()
                inner = ", ".join(f"{k}={repr(v)}" for k, v in data.items())
                return f"{val.__class__.__name__}({inner})"
            if hasattr(val, "__dict__") and not isinstance(val, (dict, list, str, int, float, bool)):
                inner = ", ".join(f"{k}={repr(v)}" for k, v in val.__dict__.items() if not k.startswith("_"))
                return f"{val.__class__.__name__}({inner})"
            return repr(val)

        return ", ".join([f"{k}={safe_repr(v)}" for k, v in bind_args.items()])

    @wraps(func)
    async def async_wrapper(*args, **kwargs):
        func_name = func.__name__
        param_str = format_args(args, kwargs)

        if param_str:
            logger.info(f"Start to call {func_name}, params: ({param_str}).")
        else:
            logger.info(f"Start to call {func_name}.")

        try:
            result = await func(*args, **kwargs)
            logger.debug(f"Call {func_name} successful, result: {result}.")
            return result
        except Exception as e:
            logger.error(f"Call {func_name} failed, error: {str(e)}")
            raise

    @wraps(func)
    def sync_wrapper(*args, **kwargs):
        func_name = func.__name__
        param_str = format_args(args, kwargs)

        if param_str:
            logger.info(f"Start to call {func_name}, params: ({param_str}).")
        else:
            logger.info(f"Start to call {func_name}.")

        try:
            result = func(*args, **kwargs)
            logger.info(f"Call {func_name} successful, result: {result}.")
            return result
        except Exception as e:
            logger.error(f"Call {func_name} failed, error: {str(e)}")
            raise

    return async_wrapper if is_async else sync_wrapper
