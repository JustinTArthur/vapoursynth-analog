"""Pure-Python tests for the decode_4fsc_video wrapper contract.

These exercise validation and model resolution without loading the plugin or
touching a capture, so they run in any environment.
"""

from __future__ import annotations

import inspect

import pytest

import vsanalog


def test_signature_has_new_kwargs():
    params = inspect.signature(vsanalog.decode_4fsc_video).parameters
    for name in (
        "color_family",
        "color_difference_precision",
        "broadcast_scaling_precision", "model_version", "model_path",
        "model_input_scale", "model_precision", "onnx_provider",
        "model_chroma_bandpass",
        "first_active_sample", "last_active_sample",
        "first_active_line", "last_active_line",
    ):
        assert name in params, f"missing kwarg {name}"


def test_padding_multiple_removed():
    params = inspect.signature(vsanalog.decode_4fsc_video).parameters
    assert "padding_multiple" not in params


@pytest.mark.parametrize("kwarg", ["first_active_line", "last_active_line"])
def test_non_positive_line_bounds_rejected(kwarg):
    # Signal line numbers are 1-indexed, so there is no line 0 or below.
    with pytest.raises(ValueError):
        vsanalog.decode_4fsc_video("x.tbc", **{kwarg: 0})


@pytest.mark.parametrize("kwarg", ["first_active_sample", "last_active_sample"])
def test_negative_sample_bounds_accepted(kwarg):
    # A negative sample number reaches back into the line blanking ahead of the
    # digital active line, so the wrapper must pass it through. The call still
    # fails on the missing capture, which is the plugin's complaint, not a
    # rejected bound.
    with pytest.raises(Exception) as excinfo:
        vsanalog.decode_4fsc_video("x.tbc", **{kwarg: -125})
    assert not isinstance(excinfo.value, ValueError)


@pytest.mark.parametrize("kwarg,value", [
    ("color_family", "banana"),
    ("color_difference_precision", "ultra"),
    ("broadcast_scaling_precision", "ultra"),
])
def test_invalid_choices_rejected(kwarg, value):
    with pytest.raises(ValueError):
        vsanalog.decode_4fsc_video("x.tbc", **{kwarg: value})


def test_nn_kwargs_require_nn_decoder():
    with pytest.raises(ValueError):
        vsanalog.decode_4fsc_video("x.tbc", decoder="ntsc2d", model_version="v1")


def test_bandpass_only_for_luma_sep():
    with pytest.raises(ValueError):
        vsanalog.decode_4fsc_video(
            "x.tbc", decoder="nntransform3d", model_chroma_bandpass=True,
        )


def test_unknown_model_version():
    with pytest.raises(ValueError):
        vsanalog._resolve_nn_model("nntransform3d", "bogus", None)


def test_custom_model_path_missing():
    with pytest.raises(FileNotFoundError):
        vsanalog._resolve_nn_model("nntransform3d", None, "/nonexistent/m.onnx")


def test_default_versions_exist_in_registry():
    for decoder, version in vsanalog._NN_DECODER_DEFAULT_VERSION.items():
        assert version in vsanalog._NN_DECODERS[decoder]


def test_nntransform3d_v2_scale():
    # v2 weights are trained at 16-bit and need the /128 input scale.
    _, scale, _ = vsanalog._NN_DECODERS["nntransform3d"]["v2"]
    assert scale == 128.0


def test_only_nntransform3d_v2_is_fp16_safe():
    # The same /128 scale is what keeps v2's spectrum inside fp16 range. Every
    # other bundled model overflows it or breaks on fp16 index math, and an
    # fp16 engine built from those returns NaN rather than failing.
    fp16 = {
        (decoder, version)
        for decoder, versions in vsanalog._NN_DECODERS.items()
        for version, (_, _, safe) in versions.items()
        if safe
    }
    assert fp16 == {("nntransform3d", "v2")}


def test_custom_model_path_is_not_assumed_fp16_safe(tmp_path):
    weights = tmp_path / "custom.onnx"
    weights.write_bytes(b"")
    _, _, fp16_safe = vsanalog._resolve_nn_model("nntransform3d", None, weights)
    assert not fp16_safe


@pytest.mark.parametrize("value", ["fp8", "FP16", "float16", ""])
def test_invalid_model_precision_rejected(value):
    with pytest.raises(ValueError):
        vsanalog.decode_4fsc_video(
            "x.tbc", decoder="nntransform3d", model_precision=value,
        )


@pytest.mark.parametrize("provider", ["cuda", "gpu", "directml"])
def test_fp16_sibling_used_when_bundled(tmp_path, provider):
    # TensorRT gets fp16 via an engine-level flag on the fp32 file; CUDA and
    # DirectML have no such flag, so an explicit pin to one of them swaps in
    # the pre-converted sibling when convert_models_fp16.py produced one.
    base = tmp_path / "chroma_net-v2-202605.onnx"
    base.write_bytes(b"")
    sibling = tmp_path / "chroma_net-v2-202605-fp16.onnx"
    sibling.write_bytes(b"")
    assert vsanalog._maybe_fp16_sibling(str(base), provider, "fp16") == str(sibling)


@pytest.mark.parametrize("provider", ["cuda", "gpu", "directml"])
def test_fp16_sibling_falls_back_when_not_bundled(tmp_path, provider):
    base = tmp_path / "chroma_net-v2-202605.onnx"
    base.write_bytes(b"")
    assert vsanalog._maybe_fp16_sibling(str(base), provider, "fp16") == str(base)


@pytest.mark.parametrize("provider", [None, "auto", "tensorrt", "trt", "migraphx", "cpu"])
def test_fp16_sibling_not_used_outside_pinned_cuda_directml(tmp_path, provider):
    # TensorRT acts on model_precision itself against the fp32 file; an
    # unpinned "auto" request must not risk handing TensorRT a
    # fp16-native file it shares the session with CUDA for (TensorRT is
    # tried first in the auto chain).
    base = tmp_path / "chroma_net-v2-202605.onnx"
    base.write_bytes(b"")
    (tmp_path / "chroma_net-v2-202605-fp16.onnx").write_bytes(b"")
    assert vsanalog._maybe_fp16_sibling(str(base), provider, "fp16") == str(base)


def test_fp16_sibling_not_used_at_fp32_precision(tmp_path):
    base = tmp_path / "chroma_net-v2-202605.onnx"
    base.write_bytes(b"")
    (tmp_path / "chroma_net-v2-202605-fp16.onnx").write_bytes(b"")
    assert vsanalog._maybe_fp16_sibling(str(base), "cuda", "fp32") == str(base)
