"""Structural checks on the installed wheel.

Catches packaging regressions: the subdir + manifest layout, model bundling,
the Windows-specific colocate-and-restore repack, and the absence of the
legacy ``vsanalog.libs/`` top-level dir on Windows.
"""
import platform
import sysconfig
from pathlib import Path

import pytest


def _site() -> Path:
    return Path(sysconfig.get_paths()["purelib"])


def _plugin_dir() -> Path:
    return _site() / "vapoursynth" / "plugins" / "vsanalog"


def _plugin_suffix() -> str:
    return {"Windows": ".dll", "Darwin": ".dylib"}.get(platform.system(), ".so")


def test_plugin_subdir_exists():
    assert _plugin_dir().is_dir(), f"plugin subdir missing: {_plugin_dir()}"


def test_plugin_library_present():
    p = _plugin_dir() / f"vsanalog{_plugin_suffix()}"
    assert p.is_file(), f"plugin library missing: {p}"


def test_manifest_present_and_valid():
    m = _plugin_dir() / "manifest.vs"
    assert m.is_file(), f"manifest.vs missing: {m}"
    lines = [ln for ln in m.read_text().splitlines() if ln.strip()]
    assert lines and lines[0] == "[VapourSynth Manifest V1]", \
        f"bad manifest header: {lines[:1]!r}"
    assert "vsanalog" in lines[1:], \
        f"manifest does not list vsanalog: {lines}"


def test_bundled_models_present():
    models = _site() / "vsanalog" / "models"
    assert models.is_dir(), f"models dir missing: {models}"
    assert (models / "nntransform3d_v2.onnx").is_file()


def test_windows_deps_colocated():
    if platform.system() != "Windows":
        pytest.skip("Windows-only layout check")
    pd = _plugin_dir()
    ort = list(pd.glob("onnxruntime-*.dll"))
    assert len(ort) == 1, \
        f"expected exactly one mangled onnxruntime DLL in {pd}, got {ort}"
    assert (pd / "onnxruntime_providers_shared.dll").is_file(), \
        "onnxruntime_providers_shared.dll missing from plugin dir"


def test_windows_no_top_level_libs_dir():
    if platform.system() != "Windows":
        pytest.skip("Windows-only layout check")
    libs = _site() / "vsanalog.libs"
    assert not libs.exists(), (
        f"{libs} should have been folded into the plugin subdir by "
        "tools/colocate_plugin_libs.py"
    )


def test_windows_init_has_no_delvewheel_patch():
    if platform.system() != "Windows":
        pytest.skip("Windows-only check")
    init = _site() / "vsanalog" / "__init__.py"
    assert init.is_file(), f"missing: {init}"
    text = init.read_text(encoding="utf-8")
    hits = [
        (i, ln) for i, ln in enumerate(text.splitlines(), 1)
        if "_delvewheel_patch" in ln
    ]
    assert not hits, (
        f"delvewheel search-path patch leaked back into {init} "
        "(the colocate repack must restore the pristine wrapper module):\n"
        + "\n".join(f"  line {i}: {ln}" for i, ln in hits[:5])
    )