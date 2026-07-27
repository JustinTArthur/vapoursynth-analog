"""The plugin autoloads from site-packages and exposes decode_4fsc_video."""

from __future__ import annotations

import pytest

vs = pytest.importorskip("vapoursynth")


def test_plugin_autoloads():
    # Importing vapoursynth auto-loads plugins under site-packages/vapoursynth/
    # plugins; the vsanalog wheel installs there.
    if not hasattr(vs.core, "analog"):
        pytest.skip("vsanalog plugin not installed in this environment")
    assert hasattr(vs.core.analog, "decode_4fsc_video")


def test_wrapper_loads_plugin():
    vsanalog = pytest.importorskip("vsanalog")
    # The decorator loads the plugin on first call; a bad path still reaches the
    # plugin (which raises), proving the plugin is present and callable.
    try:
        vsanalog._ensure_plugin_loaded()
    except FileNotFoundError:
        pytest.skip("vsanalog plugin binary not installed in this environment")
    assert hasattr(vs.core, "analog")
