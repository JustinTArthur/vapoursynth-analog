"""Shared fixtures for the vsanalog test suite.

Integration tests need a real TBC/CVBS capture. Point ``VSANALOG_TEST_NTSC_TBC``
(and optionally ``VSANALOG_TEST_PAL_TBC`` / ``VSANALOG_TEST_SECAM_TBC``) at a
capture to enable them; without it those tests skip. The pure-Python wrapper
tests need no sample and always run.
"""

from __future__ import annotations

import os
from pathlib import Path

import pytest


def _env_path(var: str) -> Path | None:
    val = os.environ.get(var)
    if not val:
        return None
    p = Path(val).expanduser()
    return p if p.exists() else None


@pytest.fixture(scope="session")
def ntsc_tbc() -> Path:
    p = _env_path("VSANALOG_TEST_NTSC_TBC")
    if p is None:
        pytest.skip("set VSANALOG_TEST_NTSC_TBC to an NTSC capture to run this test")
    return p


@pytest.fixture(scope="session")
def pal_tbc() -> Path:
    p = _env_path("VSANALOG_TEST_PAL_TBC")
    if p is None:
        pytest.skip("set VSANALOG_TEST_PAL_TBC to a PAL capture to run this test")
    return p


@pytest.fixture(scope="session")
def secam_yc() -> tuple[Path, Path]:
    """A SECAM luma+chroma (VHS color-under) capture pair.

    SECAM chroma lives in a separate ``*_chroma.tbc``; decoding the luma alone
    as composite SECAM demodulates nothing and rails the chroma, so the test
    needs both. Set both env vars to enable it.
    """
    luma = _env_path("VSANALOG_TEST_SECAM_TBC")
    chroma = _env_path("VSANALOG_TEST_SECAM_CHROMA_TBC")
    if luma is None or chroma is None:
        pytest.skip(
            "set VSANALOG_TEST_SECAM_TBC and VSANALOG_TEST_SECAM_CHROMA_TBC to a "
            "SECAM luma + chroma capture pair to run this test"
        )
    return luma, chroma
