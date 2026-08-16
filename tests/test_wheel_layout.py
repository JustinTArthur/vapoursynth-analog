"""Structural checks on an installed wheel.

Catches packaging regressions the functional tests can't see: the plugin
subdirectory and its manifest, model bundling, and on Windows the
colocate-and-restore repack that puts the vendored DLLs where VapourSynth's
autoloader can resolve them.

These assert against a wheel that is actually installed, so they only run when
``VSANALOG_CHECK_WHEEL_LAYOUT`` is set — CI sets it in the smoke test right
after installing the wheel. A source checkout has no wheel layout to check and
skips instead. Deliberately not a skip-if-absent guard on the layout itself:
the point is to fail when a wheel is installed but laid out wrong, which is how
a Windows wheel whose plugin could not autoload shipped green.
"""

from __future__ import annotations

import os
import platform
import sysconfig
from pathlib import Path

import pytest

pytestmark = pytest.mark.skipif(
    not os.environ.get("VSANALOG_CHECK_WHEEL_LAYOUT"),
    reason="set VSANALOG_CHECK_WHEEL_LAYOUT to check an installed wheel's layout",
)


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
    # VapourSynth appends the platform's library extension to each entry.
    assert "vsanalog" in lines[1:], \
        f"manifest does not list vsanalog: {lines}"


def test_bundled_models_present():
    """Every model the wrapper offers resolves to a bundled artifact."""
    vsanalog = pytest.importorskip("vsanalog")
    assert (_site() / "vsanalog" / "models").is_dir()
    for decoder, versions in vsanalog._NN_DECODERS.items():
        for version, (rel_onnx, *_rest) in versions.items():
            path = vsanalog._bundled_model_path(rel_onnx)
            assert path.exists(), f"{decoder} {version}: missing {path}"


def test_windows_deps_colocated():
    if platform.system() != "Windows":
        pytest.skip("Windows-only layout check")
    pd = _plugin_dir()
    # vsanalog.avx2.dll and friends are plugin builds, not dependencies; counting
    # one would let this pass on a wheel whose vendored DLLs never got moved.
    deps = [p for p in pd.glob("*.dll") if not p.name.startswith("vsanalog.")]
    assert deps, (
        f"no dependency DLLs beside the plugin in {pd}; "
        "tools/colocate_plugin_libs.py did not run or found nothing to move"
    )


def test_windows_no_import_library():
    if platform.system() != "Windows":
        pytest.skip("Windows-only layout check")
    # Meson installs the plugin's .lib beside the .dll with no way to opt out;
    # nothing links against a plugin, so the repack drops it.
    libs = sorted(p.name for p in _plugin_dir().glob("*.lib"))
    assert not libs, (
        f"import librar(y/ies) shipped beside the plugin: {libs}; "
        "tools/colocate_plugin_libs.py should have dropped them"
    )


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
    # The patch points at the directory the repack deletes, so leaving it in
    # would put a stale vsanalog.libs/ back on the DLL search path.
    assert not hits, (
        f"delvewheel search-path patch leaked back into {init} "
        "(the colocate repack must restore the pristine wrapper module):\n"
        + "\n".join(f"  line {i}: {ln}" for i, ln in hits[:5])
    )
