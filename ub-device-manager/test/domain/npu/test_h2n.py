"""H2N sysfs probe and wait tests."""

from pathlib import Path

import pytest

from ub_device_manager.domain.npu.h2n import _probe, wait_h2n_healthy


def _make_pfe(root: Path, name: str, guid: str, state: str) -> None:
    device = root / name
    device.mkdir()
    (device / "guid").write_text(guid, encoding="utf-8")
    (device / "fault_mgr_state").write_text(state, encoding="utf-8")


def test_probe_requires_every_pfe_healthy(tmp_path):
    _make_pfe(tmp_path, "ub0", "guid-a", "PF/IDLE")
    _make_pfe(tmp_path, "ub1", "guid-b", "FAULT")

    assert _probe(["guid-a"], tmp_path)[0] is True
    assert _probe(["guid-a", "guid-b"], tmp_path)[0] is False


async def test_wait_returns_after_stable_healthy_period(tmp_path):
    _make_pfe(tmp_path, "ub0", "guid-a", "PPF/IDLE")
    await wait_h2n_healthy(["guid-a"], {
        "sysfs_root": str(tmp_path), "wait_timeout": 1,
        "probe_timeout": 0.2, "poll_interval": 0.01,
        "healthy_stable_duration": 0.02,
    })


async def test_wait_times_out_when_link_unhealthy(tmp_path):
    _make_pfe(tmp_path, "ub0", "guid-a", "FAULT")
    with pytest.raises(TimeoutError, match="H2N wait timed out"):
        await wait_h2n_healthy(["guid-a"], {
            "sysfs_root": str(tmp_path), "wait_timeout": 0.04,
            "probe_timeout": 0.02, "poll_interval": 0.01,
            "healthy_stable_duration": 0,
        })
