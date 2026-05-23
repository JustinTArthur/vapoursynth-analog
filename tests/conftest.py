"""Shared fixtures for vsanalog smoke tests.

Fixture TBCs come from the tbc-tools submodule's test-data tree, which
``tools/init_submodule_sparse.sh`` checks out for CI. Tests skip cleanly if
those files are absent so the suite is still runnable on a stripped checkout.
"""
from pathlib import Path

import pytest

_REPO_ROOT = Path(__file__).resolve().parent.parent
_TBC_DATA = _REPO_ROOT / "extern" / "tbc-tools" / "test-data"


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