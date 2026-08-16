"""VapourSynth plugin for working with digitized analog video signals."""

from __future__ import annotations

import platform
from collections.abc import Sequence
from importlib.metadata import PackageNotFoundError, version as _get_version
from pathlib import Path
from typing import Any

# GPU wheel variants ship a vendor-runtime preload module at the site-packages
# root, normally run by its .pth at interpreter startup. Run it here too —
# idempotently, and before the vapoursynth import below can create a core and
# autoload the plugin — for interpreters that skipped site processing.
try:
    import _vsanalog_gpu_preload
except ImportError:
    pass
else:
    _vsanalog_gpu_preload.preload()

import vapoursynth as vs

from .plugin import requires_plugin
from .colorimetry import amplify_chroma, modernize_chromaticity
from .dropouts import (
    DropoutOrigin, DropoutSpan, create_dropouts_mask, dropout_spans,
)
from .secam import fill_secam_by_delay, resample_secam

__all__ = [
    "DropoutOrigin", "DropoutSpan", "amplify_chroma", "create_dropouts_mask",
    "decode_4fsc_video", "dropout_spans", "fill_secam_by_delay",
    "modernize_chromaticity", "requires_plugin", "resample_secam",
    "set_log_level",
]

try:
    __version__ = _get_version("vsanalog")
except PackageNotFoundError:
    # Imported straight from a source tree, as the docs build does.
    __version__ = "unknown"

# --- Output / color-science option value sets (validated before pass-through) ---
_COLOR_FAMILIES = {"yuv", "rgb", "gray"}
_COLOR_DIFF_PRECISIONS = {"classic", "modern"}
_BROADCAST_SCALING_PRECISIONS = {"classic", "modern", "scientific"}

# --- Neural-network decoders ---------------------------------------------------
# Model weights ship inside the wheel under models/. On macOS the bundled
# artifact is a native CoreML .mlpackage (converted from the ONNX at build time);
# every other platform bundles the .onnx and runs it through ONNX Runtime. The
# Apple-silicon nnTransform3D v2 package is converted at fp16 so it can run on
# the Neural Engine; see tools/convert_models_macos.py.
_MODELS_DIR = Path(__file__).resolve().parent / "models"

# decoder -> {model_version: (relative .onnx path, nn_input_magnitude_scale,
#                             fp16_safe)}
#
# fp16_safe marks weights whose input contract keeps every tensor inside fp16's
# range, which is a property of the training scale rather than of the graph: the
# nnTransform3D v2 weights divide their input magnitudes by 128 precisely so the
# spectrum fits, while the v1 series feeds unscaled magnitudes that overflow into
# NaN masks and the ldzeug2 models break on fp16 index math. It drives both the
# macOS conversion precision (tools/convert_models_macos.py) and the plugin's
# model_precision.
_NN_DECODERS: dict[str, dict[str, tuple[str, float, bool]]] = {
    "nntransform3d": {
        "v1_202512": ("nntransform3d/chroma_net-v1-202512.onnx", 1.0, False),
        "v1_202603": ("nntransform3d/chroma_net-v1-202603.onnx", 1.0, False),
        "v2": ("nntransform3d/chroma_net-v2-202605.onnx", 128.0, True),
    },
    "ldzeug2_color_cnn": {
        "1031640": ("ldzeug/color_cnn_1031640.onnx", 1.0, False),
        "denoise_613928_ft22k": (
            "ldzeug/color_cnn_denoise_613928_ft22k.onnx", 1.0, False,
        ),
        "v2_alot": ("ldzeug/color_cnn_v2_alot.onnx", 1.0, False),
    },
    "ldzeug2_luma_sep": {
        "2dgray_fields": ("ldzeug/luma_sep_2dgray_fields.onnx", 1.0, False),
    },
    "ldzeug2_luma_sep_frame": {
        "2d_frame_gray_gray_run2_latest": (
            "ldzeug/luma_sep_2d_frame_gray_gray_run2_latest.onnx", 1.0, False,
        ),
    },
}

_NN_DECODER_DEFAULT_VERSION = {
    "nntransform3d": "v2",
    "ldzeug2_color_cnn": "v2_alot",
    "ldzeug2_luma_sep": "2dgray_fields",
    "ldzeug2_luma_sep_frame": "2d_frame_gray_gray_run2_latest",
}

# Only ldzeug2_luma_sep* honour the chroma-bandpass toggle.
_BANDPASS_DECODERS = {"ldzeug2_luma_sep", "ldzeug2_luma_sep_frame"}

# Execution providers accepted by the plugin. Aliases (gpu->cuda, trt->tensorrt)
# are resolved by libchromadec; an unavailable accelerator falls back to CPU.
_NN_PROVIDERS = frozenset({
    "auto", "cpu", "cuda", "gpu", "tensorrt", "trt", "migraphx", "directml", "coreml",
})

# Providers with no engine-level fp16 mode (unlike TensorRT's
# trt_fp16_enable): their only route to fp16 is a pre-converted .onnx
# sibling (tools/convert_models_fp16.py). Explicit pin only, never "auto":
# TensorRT is tried ahead of CUDA on the same session, and handed the
# fp16-native file it runs at its fp32 speed — half of what
# trt_fp16_enable on the fp32 file delivers (measured on an A10G).
_FP16_NATIVE_ONLY_PROVIDERS = frozenset({"cuda", "gpu", "directml"})

_NN_PRECISIONS = {"fp32", "fp16"}


@requires_plugin
def set_log_level(level: str) -> None:
    """Set the threshold for the decoder's diagnostic messages.

    Decoding diagnostics — an accelerated neural-network backend falling back
    to CPU, a SECAM field ident that disagrees with the sidecar, a capture that
    isn't at a 4𝑓𝑠𝑐 sample rate — are reported as VapourSynth log messages, so
    :py:meth:`core.add_log_handler <Core.add_log_handler>` receives them
    alongside everything else. Failures
    are not: those raise. ``level`` is one of ``debug``, ``info`` (the
    default), ``warning``, ``critical`` or ``off``, and applies process-wide.

    Outside a host that installs its own log handler (``vspipe`` and the
    like), VapourSynth's Python module forwards these to the standard
    library's ``logging`` module under the logger name ``"vapoursynth"``. If
    nothing has called ``logging.basicConfig()`` or otherwise attached a
    handler, only ``warning`` and above reach stderr; ``debug``/``info`` are
    silently discarded.
    """
    vs.core.analog.set_log_level(level=level)


def _bundled_model_path(rel_onnx: str) -> Path:
    """Resolve a registry entry to the bundled artifact for this platform.

    macOS bundles a native CoreML ``.mlpackage`` (a directory); every other
    platform bundles the ``.onnx`` file.
    """
    if platform.system() == "Darwin":
        return _MODELS_DIR / (Path(rel_onnx).with_suffix(".mlpackage"))
    return _MODELS_DIR / rel_onnx


def _resolve_nn_model(
    decoder: str,
    model_version: str | None,
    model_path: str | Path | None,
) -> tuple[str, float, bool]:
    """Return ``(path, nn_input_magnitude_scale, fp16_safe)`` for an NN decoder.

    A user-supplied ``model_path`` wins and is used verbatim (scale 1.0, and not
    assumed fp16-safe — nothing here can inspect its training scale); otherwise
    the bundled model for ``model_version`` (or the decoder default) is located,
    choosing the ``.mlpackage`` on macOS and the ``.onnx`` elsewhere.
    """
    if model_path is not None:
        path = Path(model_path).expanduser()
        if not path.exists():
            raise FileNotFoundError(f"model_path does not exist: {path}")
        return str(path), 1.0, False

    versions = _NN_DECODERS[decoder]
    version = model_version or _NN_DECODER_DEFAULT_VERSION[decoder]
    if version not in versions:
        valid = ", ".join(sorted(versions))
        raise ValueError(
            f"Unknown model_version {version!r} for decoder {decoder!r}. "
            f"Valid versions: {valid}"
        )
    rel_onnx, scale, fp16_safe = versions[version]
    path = _bundled_model_path(rel_onnx)
    if not path.exists():
        raise FileNotFoundError(
            f"Bundled model not found at {path}. The vsanalog wheel may have "
            "been built without neural-network models."
        )
    return str(path), scale, fp16_safe


def _maybe_fp16_sibling(path: str, provider: str | None, precision: str) -> str:
    """Swap to a pre-converted ``<stem>-fp16.onnx`` sibling of ``path``, for
    providers in ``_FP16_NATIVE_ONLY_PROVIDERS`` requesting fp16. Falls back
    to ``path`` unless the sibling was actually bundled (e.g. an older wheel,
    or a decoder/version convert_models_fp16.py skipped).
    """
    if precision != "fp16" or provider not in _FP16_NATIVE_ONLY_PROVIDERS:
        return path
    p = Path(path)
    sibling = p.with_name(f"{p.stem}-fp16{p.suffix}")
    return str(sibling) if sibling.exists() else path


def _validate_choice(name: str, value: str | None, allowed: set[str]) -> None:
    if value is not None and value not in allowed:
        raise ValueError(
            f"{name} must be one of {sorted(allowed)}, got {value!r}"
        )


@requires_plugin
def decode_4fsc_video(
    composite_or_luma_source: str | Path,
    chroma_or_pb_source: str | Path | None = None,
    pr_source: str | Path | None = None,
    *,
    decoder: str | None = None,
    color_family: str | None = None,
    color_difference_precision: str | None = None,
    broadcast_scaling_precision: str | None = None,
    model_version: str | None = None,
    model_path: str | Path | None = None,
    model_input_scale: float | None = None,
    model_precision: str | None = None,
    onnx_provider: str | None = None,
    model_chroma_bandpass: bool | None = None,
    reverse_fields: bool = False,
    chroma_gain: float = 1.0,
    chroma_phase: float = 0.0,
    chroma_nr: float = 0.0,
    luma_nr: float = 0.0,
    phase_compensation: bool = True,
    first_active_sample: int | None = None,
    last_active_sample: int | None = None,
    first_active_line: int | None = None,
    last_active_line: int | None = None,
    dropout_correct: bool = False,
    dropout_overcorrect: bool = False,
    dropout_intra: bool = False,
    annotate_dropouts: bool = False,
    dropout_composite_or_luma_extra_sources: Sequence[str | Path] | None = None,
    dropout_chroma_extra_sources: Sequence[str | Path] | None = None,
) -> vs.VideoNode:
    """Decode 4𝑓𝑠𝑐 (four times subcarrier frequency) digitized analog video.

    Reads time-base-corrected (TBC) captures produced by ld-decode / vhs-decode
    (``.tbc``) or the newer CVBS format (``.cvbs`` composite,
    ``.cvbsy``/``.cvbsc`` luma/chroma) and returns a 32-bit float VapourSynth
    clip.

    The source format is detected from the file extension. RAW CVBS encodings
    (unscaled ADC captures) are not supported and are rejected.

    """
    _validate_choice("color_family", color_family, _COLOR_FAMILIES)
    _validate_choice(
        "color_difference_precision", color_difference_precision,
        _COLOR_DIFF_PRECISIONS,
    )
    _validate_choice(
        "broadcast_scaling_precision", broadcast_scaling_precision,
        _BROADCAST_SCALING_PRECISIONS,
    )

    decoder_lower = decoder.lower() if decoder else None
    is_nn_decoder = decoder_lower in _NN_DECODERS

    # Guard NN-only knobs against non-NN decoders.
    nn_only = {
        "model_version": model_version,
        "model_path": model_path,
        "model_input_scale": model_input_scale,
        "model_precision": model_precision,
        "onnx_provider": onnx_provider,
    }
    if not is_nn_decoder:
        set_nn = [k for k, v in nn_only.items() if v is not None]
        if set_nn:
            raise ValueError(
                f"{set_nn} require a neural-network decoder; valid NN decoders "
                f"are {sorted(_NN_DECODERS)}"
            )
    if model_chroma_bandpass is not None and decoder_lower not in _BANDPASS_DECODERS:
        raise ValueError(
            "model_chroma_bandpass only applies to the "
            f"{sorted(_BANDPASS_DECODERS)} decoders"
        )
    if model_input_scale is not None and model_input_scale <= 0:
        raise ValueError("model_input_scale must be positive")
    _validate_choice("model_precision", model_precision, _NN_PRECISIONS)

    kwargs: dict[str, Any] = {}

    if chroma_or_pb_source is not None:
        kwargs["chroma_or_pb_source"] = chroma_or_pb_source
    if pr_source is not None:
        kwargs["pr_source"] = pr_source
    if decoder is not None:
        kwargs["decoder"] = decoder_lower
    if color_family is not None:
        kwargs["color_family"] = color_family
    if color_difference_precision is not None:
        kwargs["color_difference_precision"] = color_difference_precision
    if broadcast_scaling_precision is not None:
        kwargs["broadcast_scaling_precision"] = broadcast_scaling_precision

    if is_nn_decoder:
        assert decoder_lower is not None
        provider = None
        if onnx_provider is not None:
            provider = onnx_provider.strip().lower()
            if provider not in _NN_PROVIDERS:
                raise ValueError(
                    f"onnx_provider must be one of {sorted(_NN_PROVIDERS)}, "
                    f"got {onnx_provider!r}"
                )
            kwargs["onnx_provider"] = provider

        path, registry_scale, fp16_safe = _resolve_nn_model(
            decoder_lower, model_version, model_path,
        )
        # Explicit override wins over the registry scale; only forward a
        # non-default scale to keep the call clean.
        scale = model_input_scale if model_input_scale is not None else registry_scale
        if scale != 1.0:
            kwargs["model_input_scale"] = scale
        # fp16-safe weights allow it by default: TensorRT builds a
        # mixed-precision engine from this same fp32 file, CoreML ships an
        # fp16 package outright, and an explicit CUDA/DirectML pin swaps in
        # a pre-converted fp16 file (_maybe_fp16_sibling). Backends with no
        # fp16 mode — CPU, and MIGraphX, where it measured ~6.8x slower on
        # gfx906/gfx900 and so was left unwired — ignore the flag.
        precision = model_precision or ("fp16" if fp16_safe else "fp32")
        if precision != "fp32":
            kwargs["model_precision"] = precision
        kwargs["model_path"] = _maybe_fp16_sibling(path, provider, precision)
        if model_chroma_bandpass is not None:
            kwargs["model_chroma_bandpass"] = int(bool(model_chroma_bandpass))

    # Crop bounds: forward only what the caller set, so each unset bound keeps
    # the interface standard's value. Sample numbers may be negative — that
    # reaches back into the line blanking ahead of the digital active line —
    # but signal line numbers are 1-indexed and have no such extension.
    for name, bound in (
        ("first_active_sample", first_active_sample),
        ("last_active_sample", last_active_sample),
    ):
        if bound is not None:
            kwargs[name] = bound
    for name, bound in (
        ("first_active_line", first_active_line),
        ("last_active_line", last_active_line),
    ):
        if bound is not None:
            if bound < 1:
                raise ValueError(f"{name} must be 1 or greater, got {bound}")
            kwargs[name] = bound

    if dropout_composite_or_luma_extra_sources is not None:
        kwargs["dropout_composite_or_luma_extra_sources"] = (
            dropout_composite_or_luma_extra_sources
        )
    if dropout_chroma_extra_sources is not None:
        kwargs["dropout_chroma_extra_sources"] = dropout_chroma_extra_sources

    # VapourSynth's Python bindings coerce bool→int and Path→str automatically.
    return vs.core.analog.decode_4fsc_video(
        composite_or_luma_source,
        reverse_fields=reverse_fields,
        chroma_gain=chroma_gain,
        chroma_phase=chroma_phase,
        chroma_nr=chroma_nr,
        luma_nr=luma_nr,
        phase_compensation=phase_compensation,
        dropout_correct=dropout_correct,
        dropout_overcorrect=dropout_overcorrect,
        dropout_intra=dropout_intra,
        annotate_dropouts=annotate_dropouts,
        **kwargs,
    )
