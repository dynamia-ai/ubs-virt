import os
import re
import tomllib

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


def load_settings() -> dict:
    with open(resolve_config_path(), "rb") as f:
        return tomllib.load(f)


CONFIG = load_settings()
