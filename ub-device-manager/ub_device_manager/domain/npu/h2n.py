"""Local H2N link checks used before NIC configuration."""

from __future__ import annotations

import asyncio
import re
import time
from pathlib import Path

from loguru import logger

from ub_device_manager.common.config import CONFIG, H2NMode
from ub_device_manager.domain.npu.npu_client import NpuClient, ProductType

HEALTHY_STATES = {"PF/IDLE", "PPF/IDLE"}
SAFE_DEVICE_NAME = re.compile(r"^[A-Za-z0-9][A-Za-z0-9_.:-]*$")


async def is_h2n_required(client: NpuClient, config: dict | None = None) -> bool:
    """Apply the same H2N mode and product-type policy for creation and libvirt starts."""
    config = config if config is not None else CONFIG["h2n"]
    if config["mode"] is H2NMode.DISABLED:
        return False
    if config["mode"] is H2NMode.ALL:
        return True
    if config["mode"] is H2NMode.SERVER_TYPE:
        product_type = await asyncio.wait_for(client.get_product_type(), timeout=config["probe_timeout"])
        return product_type is ProductType.SERVER
    raise ValueError(f"Unsupported H2N mode: {config['mode']!r}.")


def _probe(pfe_guids: list[str], root: Path) -> tuple[bool, str]:
    mapping = {}
    for device in root.iterdir():
        if not device.is_dir() or not SAFE_DEVICE_NAME.fullmatch(device.name):
            continue
        try:
            guid = (device / "guid").read_text(encoding="utf-8").strip().lower()
        except (OSError, UnicodeError):
            continue
        if guid in mapping:
            return False, f"Duplicate sysfs GUID: {guid}"
        mapping[guid] = device

    for guid in dict.fromkeys(pfe_guids):
        device = mapping.get(guid.strip().lower())
        if device is None:
            return False, f"PFE GUID not found: {guid}"
        try:
            state = (device / "fault_mgr_state").read_text(encoding="utf-8").strip()
        except (OSError, UnicodeError) as exc:
            return False, f"PFE {guid} state query failed: {exc}"
        if state not in HEALTHY_STATES:
            return False, f"PFE {guid} state is {state or '<empty>'}"
    return True, "All PFE links are healthy"


async def wait_h2n_healthy(pfe_guids: list[str], config: dict | None = None) -> None:
    """Wait for the first healthy probe, then require a stable healthy period."""
    if not pfe_guids or any(not isinstance(guid, str) or not guid.strip() for guid in pfe_guids):
        raise ValueError("H2N check requires nonempty PFE GUIDs.")
    config = config if config is not None else CONFIG["h2n"]
    root = Path(config["sysfs_root"])
    deadline = time.monotonic() + config["wait_timeout"]
    healthy_since = None
    last_detail = "No H2N probe completed"
    attempts = 0

    while True:
        remaining = deadline - time.monotonic()
        if healthy_since is None and remaining <= 0:
            raise TimeoutError(f"H2N wait timed out after {attempts} probes: {last_detail}.")
        attempts += 1
        timeout = config["probe_timeout"] if healthy_since is not None else min(config["probe_timeout"], remaining)
        try:
            healthy, detail = await asyncio.wait_for(asyncio.to_thread(_probe, pfe_guids, root), timeout)
        except asyncio.TimeoutError:
            healthy, detail = False, "H2N probe timed out"
        except Exception as exc:
            healthy, detail = False, f"H2N probe failed: {exc}"
        now = time.monotonic()
        if healthy:
            if healthy_since is None and now <= deadline:
                healthy_since = now
            if healthy_since is not None and now - healthy_since >= config["healthy_stable_duration"]:
                logger.info("H2N links healthy after {} probes, PFE count {}.", attempts, len(set(pfe_guids)))
                return
        else:
            healthy_since = None
        last_detail = detail
        if healthy_since is None:
            sleep_for = min(config["poll_interval"], max(deadline - now, 0))
        else:
            sleep_for = min(config["poll_interval"], max(config["healthy_stable_duration"] - (now - healthy_since), 0))
        await asyncio.sleep(sleep_for)
