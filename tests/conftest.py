"""Shared fixtures for vsanalog smoke tests.

Fixture TBCs come from the tbc-tools submodule's test-data tree, which
``tools/init_submodule_sparse.sh`` checks out for CI. Tests skip cleanly if
those files are absent so the suite is still runnable on a stripped checkout.
"""
import os
from pathlib import Path

import pytest

_REPO_ROOT = Path(__file__).resolve().parent.parent
_TBC_DATA = _REPO_ROOT / "extern" / "tbc-tools" / "test-data"


@pytest.hookimpl(trylast=True)
def pytest_sessionfinish(session, exitstatus):
    """Bypass interpreter shutdown to dodge the ONNX Runtime cleanup hang.

    ORT's static destructors / background-thread join can stall indefinitely
    at process exit (seen on Windows in CI: pytest finishes printing its
    summary, then ``python.exe`` never returns). ``trylast=True`` ensures
    pytest's terminal-summary plugin (which also runs in sessionfinish)
    prints failure tracebacks before we ``_exit`` and skip the no-op
    interpreter-shutdown stage.
    """
    os._exit(exitstatus)


def _require(p: Path) -> str:
    if not p.is_file():
        pytest.skip(
            f"test fixture missing: {p} "
            "(re-run tools/init_submodule_sparse.sh to fetch it)"
        )
    return str(p)


@pytest.fixture(scope="session")
def pal_tbc() -> str:
    """A small PAL 4fsc TBC capture (jason-testpattern: 8 fields / 4 frames)."""
    return _require(_TBC_DATA / "pal" / "jason-testpattern.tbc")


@pytest.fixture(scope="session")
def ntsc_tbc() -> str:
    """A small NTSC 4fsc TBC capture (issue176: 8 fields / 4 frames)."""
    return _require(_TBC_DATA / "ntsc" / "issue176.tbc")