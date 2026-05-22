"""VapourSynth plugin for working with digitized analog video signals."""

from __future__ import annotations

import functools
import platform
import sys
from collections.abc import Callable, Sequence
from importlib.metadata import version as _get_version
from pathlib import Path
from typing import Any, TypeVar

if sys.version_info >= (3, 10):
    from typing import ParamSpec
else:
    from typing_extensions import ParamSpec

import vapoursynth as vs

__all__ = ["decode_4fsc_video", "requires_plugin"]

__version__ = _get_version("vsanalog")

P = ParamSpec("P")
R = TypeVar("R")

# Directory holding ONNX model files bundled with the package.
_MODELS_DIR = Path(__file__).resolve().parent / "models"


# Registry of neural-network-based decoders. Keyed by the lowercase decoder
# string accepted by the plugin. Each entry maps a ``model_version`` to a
# ``(bundled .onnx path, input-magnitude scale)`` pair.
#
# The input-magnitude scale is the divisor a model's training contract
# expects on its input magnitude spectrum. nnTransform3D v2 was trained on
# inputs divided by 128 (headroom for its FP16 inference path); every other
# bundled model uses raw magnitudes (1.0). The wrapper resolves both the
# path and the scale and passes them to the plugin, which stays version-
# agnostic — it just receives a model path and a scale.
#
# To add a new neural decoder later, append an entry here and (in C++) add a
# matching ``DecoderType`` value plus the ``parseDecoderName`` mapping.
_NN_DECODERS: dict[str, dict[str, tuple[Path, float]]] = {
    "nntransform3d": {
        # Author's original v1/v2 designations. Note that tbc-tools' on-disk
        # filenames historically had these swapped; we use the author's
        # designations regardless.
        "v1": (_MODELS_DIR / "nntransform3d_v1.onnx", 1.0),
        "v2": (_MODELS_DIR / "nntransform3d_v2.onnx", 128.0),
    },
    # ldzeug2 NN luma extractor, per-field model. Version tags mirror
    # jsaowji's source filenames so each version's artifact is unambiguous.
    "ldzeug2_luma_sep": {
        "2dgray_fields": (
            _MODELS_DIR / "ldzeug2_luma_sep_2dgray_fields.onnx", 1.0
        ),
    },
    # ldzeug2 NN luma extractor, weaved-frame model. Separate decoder name
    # because frame and field are different input pipelines, not just
    # different weights — jsaowji's bundled weights advertise dynamic shapes,
    # so the plugin can't infer mode from the model alone.
    "ldzeug2_luma_sep_frame": {
        "2d_frame_gray_gray_run2_latest": (
            _MODELS_DIR / "ldzeug2_luma_sep_2d_frame_gray_gray_run2_latest.onnx",
            1.0,
        ),
    },
    # ldzeug2 joint Y/C separator + chroma demodulator. Version tags mirror
    # jsaowji's source filenames (with the redundant ``color_cnn_`` prefix
    # dropped since the decoder name conveys it).
    "ldzeug2_color_cnn": {
        "1031640": (_MODELS_DIR / "ldzeug2_color_cnn_1031640.onnx", 1.0),
        "denoise_613928_ft22k": (
            _MODELS_DIR / "ldzeug2_color_cnn_denoise_613928_ft22k.onnx", 1.0
        ),
        "v2_alot": (_MODELS_DIR / "ldzeug2_color_cnn_v2_alot.onnx", 1.0),
    },
}

# Default model version for each NN decoder when the caller doesn't specify
# one. Newer/faster releases win the default.
_NN_DECODER_DEFAULT_VERSION: dict[str, str] = {
    "nntransform3d": "v2",
    "ldzeug2_luma_sep": "2dgray_fields",
    "ldzeug2_luma_sep_frame": "2d_frame_gray_gray_run2_latest",
    "ldzeug2_color_cnn": "v2_alot",
}

# Accepted values for the ``nn_provider`` kwarg. Matches the C++ parser in
# ldzeug_decoders.cpp / patched comb.cpp. Aliases share the same handler.
# MIGraphX is the canonical AMD/HIP path; the lower-level ROCM EP is not
# exposed because AMD's recommendation and most user tooling targets MIGraphX.
_NN_PROVIDERS = frozenset({
    "auto", "cpu", "cuda", "gpu", "migraphx", "tensorrt", "trt", "coreml",
})


def _get_plugin_path() -> Path:
    """Derive the filesystem path of the bundled vsanalog shared library."""
    suffix = {"Windows": ".dll", "Darwin": ".dylib"}.get(platform.system(), ".so")
    _packages_root = Path(__file__).resolve().parent.parent
    return _packages_root / "vapoursynth" / "plugins" / f"vsanalog{suffix}"


def _ensure_plugin_loaded() -> None:
    """Load the vsanalog VapourSynth plugin if it isn't already available."""
    if not hasattr(vs.core, "analog"):
        plugin_path = _get_plugin_path()
        if not plugin_path.is_file():
            raise FileNotFoundError(
                f"vsanalog plugin not found at {plugin_path}. "
                "Ensure the vsanalog package is properly installed."
            )
        vs.core.std.LoadPlugin(plugin_path)


def requires_plugin(func: Callable[P, R]) -> Callable[P, R]:
    """Decorator ensuring the vsanalog VapourSynth plugin is loaded."""

    @functools.wraps(func)
    def wrapper(*args: P.args, **kwargs: P.kwargs) -> R:
        _ensure_plugin_loaded()
        return func(*args, **kwargs)

    return wrapper


def _resolve_nn_model(
    decoder: str,
    model_version: str | None,
    model_path: str | Path | None,
) -> tuple[str, float]:
    """Resolve a neural decoder's model to an ``(absolute path, scale)`` pair.

    ``scale`` is the input-magnitude divisor the model expects. Either
    ``model_path`` (explicit override) or ``model_version`` (selects a
    bundled file) must produce a usable path; ``model_path`` wins when both
    are supplied. Custom ``model_path`` weights carry no registry metadata,
    so their scale defaults to ``1.0`` — override it with the
    ``model_input_scale`` kwarg if the weights need a different one.
    """
    if model_path is not None:
        resolved = Path(model_path).expanduser()
        if not resolved.is_file():
            raise FileNotFoundError(
                f"model_path {resolved} does not exist or is not a regular file"
            )
        return str(resolved), 1.0

    versions = _NN_DECODERS[decoder]
    if model_version is None:
        model_version = _NN_DECODER_DEFAULT_VERSION[decoder]
    if model_version not in versions:
        valid = ", ".join(sorted(versions))
        raise ValueError(
            f"Unknown model_version {model_version!r} for decoder {decoder!r}. "
            f"Valid versions: {valid}."
        )
    bundled, scale = versions[model_version]
    if not bundled.is_file():
        raise FileNotFoundError(
            f"Bundled model file not found at {bundled}. The vsanalog "
            "package may be incomplete; reinstall the wheel."
        )
    return str(bundled), scale


@requires_plugin
def decode_4fsc_video(
    composite_or_luma_source: str | Path,
    chroma_or_pb_source: str | Path | None = None,
    pr_source: str | Path | None = None,
    *,
    decoder: str | None = None,
    model_version: str | None = None,
    model_path: str | Path | None = None,
    model_input_scale: float | None = None,
    onnx_provider: str | None = None,
    model_chroma_bandpass: bool | None = None,
    reverse_fields: bool = False,
    chroma_gain: float = 1.0,
    chroma_phase: float = 0.0,
    chroma_nr: float = 0.0,
    luma_nr: float = 0.0,
    phase_compensation: bool = False,
    padding_multiple: int = 8,
    dropout_correct: bool = False,
    dropout_overcorrect: bool = False,
    dropout_intra: bool = False,
    dropout_composite_or_luma_extra_sources: Sequence[str | Path] | None = None,
    dropout_chroma_extra_sources: Sequence[str | Path] | None = None,
    fpsnum: int | None = None,
    fpsden: int = 1,
) -> vs.VideoNode:
    """Decode 4𝑓𝑠𝑐 (four times subcarrier frequency) digitized analog video.

    Reads time-base corrected (TBC) captures produced by ld-decode or vhs-decode
    and returns a VapourSynth clip in YUV444PS or GRAYS format (32-bit float).

    Neural-network decoders (``decoder="nntransform3d"``,
    ``"ldzeug2_luma_sep"``, ``"ldzeug2_color_cnn"``) require either
    ``model_version`` to select a bundled model or ``model_path`` to point at
    a custom ONNX file. Such decoders are NTSC-only — PAL and PAL-M sources
    will be rejected.

    ``onnx_provider`` optionally pins the ONNX Runtime execution provider for
    NN decoders. Recognized values: ``"auto"`` (default; CoreML on macOS,
    CPU elsewhere), ``"cpu"``, ``"cuda"`` / ``"gpu"``, ``"migraphx"``,
    ``"tensorrt"`` / ``"trt"``, ``"coreml"``. The matching provider library
    (e.g. ``libonnxruntime_providers_cuda.so``) must be installed next to
    the bundled ``libonnxruntime`` for the request to succeed.

    ``model_chroma_bandpass`` is only meaningful with
    ``decoder="ldzeug2_luma_sep"``. When ``True`` (the default behavior on the
    plugin side), the analytical chroma demod runs the demodulated I and Q
    samples through a 17-tap low-pass FIR before deriving U/V — mirroring
    jsaowji's ``comb_split_already(..., color_bp=True)``. Set ``False`` to
    skip the filter.

    ``model_input_scale`` divides the model's input magnitude spectrum. For
    bundled models it is selected automatically per ``model_version`` (the
    ``nntransform3d`` ``v2`` weights need ``128``; everything else needs
    ``1``), so callers normally leave it unset. Supply it only with a custom
    ``model_path`` whose weights were trained against a scaled input.
    """
    kwargs: dict[str, Any] = {}

    decoder_lower = decoder.lower() if decoder is not None else None
    is_nn_decoder = decoder_lower in _NN_DECODERS

    if not is_nn_decoder and (
        model_version is not None or model_path is not None
        or onnx_provider is not None or model_input_scale is not None
    ):
        valid = ", ".join(sorted(_NN_DECODERS))
        raise ValueError(
            "model_version, model_path, onnx_provider, and model_input_scale "
            f"are only meaningful for neural-network decoders ({valid}); "
            "set decoder= first."
        )
    if model_input_scale is not None and model_input_scale <= 0:
        raise ValueError(
            f"model_input_scale must be positive, got {model_input_scale!r}."
        )
    if model_chroma_bandpass is not None and decoder_lower not in {
        "ldzeug2_luma_sep", "ldzeug2_luma_sep_frame",
    }:
        raise ValueError(
            "model_chroma_bandpass is only meaningful for "
            "decoder='ldzeug2_luma_sep' or 'ldzeug2_luma_sep_frame'."
        )
    if is_nn_decoder:
        resolved_path, resolved_scale = _resolve_nn_model(
            decoder_lower, model_version, model_path
        )
        kwargs["model_path"] = resolved_path
        # An explicit kwarg overrides the registry's per-version scale —
        # the escape hatch for custom model_path weights.
        if model_input_scale is not None:
            resolved_scale = float(model_input_scale)
        if resolved_scale != 1.0:
            kwargs["model_input_scale"] = resolved_scale
        if onnx_provider is not None:
            onnx_provider_lower = onnx_provider.strip().lower()
            if onnx_provider_lower not in _NN_PROVIDERS:
                valid = ", ".join(sorted(_NN_PROVIDERS))
                raise ValueError(
                    f"Unknown onnx_provider {onnx_provider!r}. "
                    f"Valid values: {valid}."
                )
            # nntransform3d's upstream code only wires cpu/cuda/coreml; reject
            # AMD/TRT values for it so the user gets a clear error instead of
            # silent CPU fallback.
            if (decoder_lower == "nntransform3d"
                    and onnx_provider_lower not in {"auto", "cpu", "cuda", "gpu", "coreml"}):
                raise ValueError(
                    f"onnx_provider={onnx_provider!r} is not yet supported for "
                    "decoder='nntransform3d'. Valid values for this decoder: "
                    "auto, cpu, cuda, gpu, coreml. The ldzeug2_* decoders "
                    "support the full provider set."
                )
            kwargs["onnx_provider"] = onnx_provider_lower

    if model_chroma_bandpass is not None:
        kwargs["model_chroma_bandpass"] = int(bool(model_chroma_bandpass))

    # Optional parameters — only pass when explicitly provided so the
    # C++ side can distinguish "not given" from "given as default".
    if chroma_or_pb_source is not None:
        kwargs["chroma_or_pb_source"] = chroma_or_pb_source
    if pr_source is not None:
        kwargs["pr_source"] = pr_source
    if decoder is not None:
        kwargs["decoder"] = decoder
    if dropout_composite_or_luma_extra_sources is not None:
        kwargs["dropout_composite_or_luma_extra_sources"] = (
            dropout_composite_or_luma_extra_sources
        )
    if dropout_chroma_extra_sources is not None:
        kwargs["dropout_chroma_extra_sources"] = dropout_chroma_extra_sources
    if fpsnum is not None:
        kwargs["fpsnum"] = fpsnum
        kwargs["fpsden"] = fpsden

    # VapourSynth's Python bindings handle bool→int and Path→str
    # coercion automatically, so remaining args pass through as-is.
    return vs.core.analog.decode_4fsc_video(
        composite_or_luma_source,
        reverse_fields=reverse_fields,
        chroma_gain=chroma_gain,
        chroma_phase=chroma_phase,
        chroma_nr=chroma_nr,
        luma_nr=luma_nr,
        phase_compensation=phase_compensation,
        padding_multiple=padding_multiple,
        dropout_correct=dropout_correct,
        dropout_overcorrect=dropout_overcorrect,
        dropout_intra=dropout_intra,
        **kwargs,
    )
