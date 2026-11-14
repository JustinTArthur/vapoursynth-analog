"""set_onnx_provider_library, the plugin's hook for ONNX Runtime plugin EPs.

Storing a path loads nothing, so these run without a GPU or a real plugin
library.
"""

from __future__ import annotations

import sys

import pytest

vs = pytest.importorskip("vapoursynth")


@pytest.fixture
def analog():
    plugin = pytest.importorskip("vsanalog.plugin")
    try:
        plugin._ensure_plugin_loaded()
    except FileNotFoundError:
        pytest.skip("vsanalog plugin binary not installed in this environment")
    return vs.core.analog


@pytest.mark.parametrize("provider", ["cuda", "tensorrt", "cpu", ""])
def test_only_migraphx_has_a_plugin_form(analog, provider):
    with pytest.raises(vs.Error, match="no plugin library form"):
        analog.set_onnx_provider_library(provider=provider)


def test_if_unset_leaves_a_scripts_choice_alone(analog, tmp_path):
    try:
        analog.set_onnx_provider_library(provider="migraphx")
    except vs.Error:
        pytest.skip("this build has no ONNX Runtime to name a plugin library for")
    # A missing file would raise if this call reached libchromadec.
    analog.set_onnx_provider_library(
        provider="migraphx", path=str(tmp_path / "libmigraphx-ep.so"), if_unset=True,
    )


def test_missing_library_rejected(analog, tmp_path):
    with pytest.raises(vs.Error):
        analog.set_onnx_provider_library(
            provider="migraphx", path=str(tmp_path / "libmigraphx-ep.so"),
        )


def test_existing_library_stored_without_loading(analog, tmp_path):
    # An empty file would fail to load, so success shows the path is only
    # recorded here. Plugin EPs exist only for Linux ONNX Runtime builds.
    library = tmp_path / "libmigraphx-ep.so"
    library.write_bytes(b"")
    if not sys.platform.startswith("linux"):
        with pytest.raises(vs.Error):
            analog.set_onnx_provider_library(provider="migraphx", path=str(library))
        return
    try:
        analog.set_onnx_provider_library(provider="migraphx", path=str(library))
    finally:
        analog.set_onnx_provider_library(provider="migraphx")
