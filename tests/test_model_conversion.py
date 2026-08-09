"""Checks on the macOS CoreML conversion mapping in tools/.

Pure Python: no VapourSynth, no plugin, no downloaded weights. The mapping is
worth guarding because both halves fail silently in a wheel rather than at
conversion time — a model with no shape mapping stops the build, but a model
converted at the wrong precision produces a package that loads and decodes
garbage (fp16 overflows the v1 chroma_net contract into NaN masks).
"""

from __future__ import annotations

import importlib.util
import sys
from pathlib import Path

import pytest

_ROOT = Path(__file__).resolve().parent.parent
_LOCK = _ROOT / "tools" / "models.lock"


def _load_converter():
    path = _ROOT / "tools" / "convert_models_macos.py"
    spec = importlib.util.spec_from_file_location("convert_models_macos", path)
    module = importlib.util.module_from_spec(spec)
    sys.modules[spec.name] = module
    spec.loader.exec_module(module)
    return module


conv = _load_converter()


def _lock_models() -> list[str]:
    lines = [ln.strip() for ln in _LOCK.read_text().splitlines()]
    return [ln.split()[0] for ln in lines if ln and not ln.startswith("#")]


@pytest.mark.parametrize("rel", _lock_models())
def test_every_locked_model_has_a_shape_mapping(rel):
    args = conv._convert_args(rel)
    assert args[0] == "--model-type" and args[1]


def test_unknown_model_is_rejected():
    with pytest.raises(ValueError):
        conv._convert_args("some_new_decoder/weights.onnx")


def test_only_chroma_net_v2_converts_at_fp16():
    fp16 = [r for r in _lock_models() if conv._fp16_safe(r, "always", "arm64")]
    assert fp16 == ["nntransform3d/chroma_net-v2-202605.onnx"]


@pytest.mark.parametrize("machine", ["arm64", "x86_64"])
def test_no_model_converts_at_fp16_when_disabled(machine):
    assert not any(conv._fp16_safe(r, "never", machine) for r in _lock_models())


def test_auto_is_apple_silicon_only():
    v2 = "nntransform3d/chroma_net-v2-202605.onnx"
    assert conv._fp16_safe(v2, "auto", "arm64")
    # No ANE on an Intel Mac, and fp16 on the GPU is slower than fp32 there.
    assert not conv._fp16_safe(v2, "auto", "x86_64")
