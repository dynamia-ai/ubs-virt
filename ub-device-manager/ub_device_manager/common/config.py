import os
import re
import tomllib
from enum import StrEnum

BASE_DIR = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
PACKAGED_CONFIG_PATH = os.path.join(BASE_DIR, "ub_device_manager.toml")
SYSTEM_CONFIG_PATH = "/etc/ub_device_manager/ub_device_manager.toml"
CONFIG_PATH_ENV_VAR = "UBDM_CONFIG_PATH"


def resolve_config_path() -> str:
    """Resolve which configuration file should be loaded.

    Priority: explicit ``UBDM_CONFIG_PATH`` environment variable, then the
    system configuration under ``/etc`` (the file shipped for operators), and
    finally the default copy bundled inside the package.
    """
    candidates = (
        os.environ.get(CONFIG_PATH_ENV_VAR),
        SYSTEM_CONFIG_PATH,
        PACKAGED_CONFIG_PATH,
    )
    for candidate in candidates:
        if candidate and os.path.isfile(candidate):
            return candidate
    return PACKAGED_CONFIG_PATH


CONFIG_PATH = resolve_config_path()

_SIZE_PATTERN = re.compile(r"^\s*(\d+)\s*(B|KB|MB|GB)?\s*$", re.IGNORECASE)
_SIZE_UNITS = {
    "B": 1,
    "KB": 1024,
    "MB": 1024 * 1024,
    "GB": 1024 * 1024 * 1024,
}
DEFAULT_MAX_XML_SIZE = 1024 * 1024


class H2NMode(StrEnum):
    DISABLED = "disabled"
    ALL = "all"
    SERVER_TYPE = "server_type"


DEFAULT_H2N_SETTINGS = {
    "mode": H2NMode.SERVER_TYPE.value,
    "wait_timeout": 300,
    "poll_interval": 2,
    "probe_timeout": 3,
    "healthy_stable_duration": 10,
    "sysfs_root": "/sys/bus/ub/devices",
}


def parse_size(size, default: int = DEFAULT_MAX_XML_SIZE) -> int:
    """Convert a human-readable size such as ``"1 MB"`` into bytes.

    Integers are returned as-is. Invalid values fall back to ``default``.
    """
    if isinstance(size, bool):
        return default
    if isinstance(size, int):
        return size
    match = _SIZE_PATTERN.match(str(size))
    if match is None:
        return default
    value = int(match.group(1))
    unit = (match.group(2) or "B").upper()
    return value * _SIZE_UNITS[unit]


def validate_h2n_settings(raw: dict) -> dict:
    """Normalize H2N configuration and reject invalid values at startup."""
    if not isinstance(raw, dict):
        raise ValueError("h2n must be a TOML table.")
    config = {**DEFAULT_H2N_SETTINGS, **raw}
    try:
        config["mode"] = H2NMode(config["mode"])
    except ValueError as exc:
        raise ValueError("h2n.mode must be disabled, all, or server_type.") from exc
    for key, lower, upper in (
        ("wait_timeout", 1, 3600),
        ("poll_interval", 0.1, 60),
        ("probe_timeout", 0.1, 60),
        ("healthy_stable_duration", 0, 300),
    ):
        value = config[key]
        if isinstance(value, bool) or not isinstance(value, (int, float)) or not lower <= value <= upper:
            raise ValueError(f"Invalid h2n.{key}: {value!r}.")
    if config["healthy_stable_duration"] > config["wait_timeout"]:
        raise ValueError("h2n.healthy_stable_duration exceeds wait_timeout.")
    if not isinstance(config["sysfs_root"], str) or not config["sysfs_root"]:
        raise ValueError("Invalid h2n.sysfs_root.")
    return config


def load_settings() -> dict:
    with open(resolve_config_path(), "rb") as f:
        config = tomllib.load(f)
    config["h2n"] = validate_h2n_settings(config.get("h2n", {}))
    return config


CONFIG = load_settings()
