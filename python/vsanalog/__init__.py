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
# string accepted by the plugin. Each entry describes how to resolve a
# user-supplied ``model_version`` to a bundled .onnx file path.
#
# To add a new neural decoder later, append an entry here and (in C++) add a
# matching ``DecoderType`` value plus the ``parseDecoderName`` mapping. The
# rest of the wrapper plumbing — kwargs, validation, model resolution — is
# version-agnostic.
_NN_DECODERS: dict[str, dict[str, Path]] = {
    "nntransform3d": {
        # Author's original v1/v2 designations. Note that tbc-tools' on-disk
        # filenames historically had these swapped; we use the author's
        # designations regardless.
        "v1": _MODELS_DIR / "nntransform3d_v1.onnx",
        "v2": _MODELS_DIR / "nntransform3d_v2.onnx",
    },
    # ldzeug2 luma-only separator. NN replaces step 1 of the NTSC decode
    # chain (Y/C separation); chroma is recovered as ``CVBS - luma`` and
    # passed through a 2D analytical comb downstream. ``field`` weights
    # are trained on per-field gray, ``frame`` weights on weaved frames.
    "ldzeug2_luma_sep": {
        "field": _MODELS_DIR / "ldzeug2_luma_sep_field.onnx",
        "frame": _MODELS_DIR / "ldzeug2_luma_sep_frame.onnx",
    },
    # ldzeug2 joint Y/C separator + chroma demodulator. NN takes
    # ``[CVBS, I-carrier, Q-carrier]`` and emits ``[Y, I, Q]`` directly,
    # replacing steps 1+2 of the NTSC chain. No comb runs downstream.
    "ldzeug2_color_cnn": {
        "v1": _MODELS_DIR / "ldzeug2_color_cnn_v1.onnx",
        "v1_denoise": _MODELS_DIR / "ldzeug2_color_cnn_v1_denoise.onnx",
        "v2": _MODELS_DIR / "ldzeug2_color_cnn_v2.onnx",
    },
}

# Default model version for each NN decoder when the caller doesn't specify
# one. Newer/faster releases win the default.
_NN_DECODER_DEFAULT_VERSION: dict[str, str] = {
    "nntransform3d": "v2",
    "ldzeug2_luma_sep": "field",
    "ldzeug2_color_cnn": "v2",
}


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


def _resolve_nn_model_path(
    decoder: str,
    model_version: str | None,
    model_path: str | Path | None,
) -> str:
    """Resolve a neural decoder's model location to an absolute path string.

    Either ``model_path`` (explicit override) or ``model_version`` (selects a
    bundled file) must produce a usable path. ``model_path`` wins when both
    are supplied.
    """
    if model_path is not None:
        resolved = Path(model_path).expanduser()
        if not resolved.is_file():
            raise FileNotFoundError(
                f"model_path {resolved} does not exist or is not a regular file"
            )
        return str(resolved)

    versions = _NN_DECODERS[decoder]
    if model_version is None:
        model_version = _NN_DECODER_DEFAULT_VERSION[decoder]
    if model_version not in versions:
        valid = ", ".join(sorted(versions))
        raise ValueError(
            f"Unknown model_version {model_version!r} for decoder {decoder!r}. "
            f"Valid versions: {valid}."
        )
    bundled = versions[model_version]
    if not bundled.is_file():
        raise FileNotFoundError(
            f"Bundled model file not found at {bundled}. The vsanalog "
            "package may be incomplete; reinstall the wheel."
        )
    return str(bundled)


@requires_plugin
def decode_4fsc_video(
    composite_or_luma_source: str | Path,
    chroma_or_pb_source: str | Path | None = None,
    pr_source: str | Path | None = None,
    *,
    decoder: str | None = None,
    model_version: str | None = None,
    model_path: str | Path | None = None,
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
    """
    kwargs: dict[str, Any] = {}

    decoder_lower = decoder.lower() if decoder is not None else None
    is_nn_decoder = decoder_lower in _NN_DECODERS

    if not is_nn_decoder and (model_version is not None or model_path is not None):
        valid = ", ".join(sorted(_NN_DECODERS))
        raise ValueError(
            "model_version and model_path are only meaningful for "
            f"neural-network decoders ({valid}); set decoder= first."
        )
    if is_nn_decoder:
        kwargs["nn_model_path"] = _resolve_nn_model_path(
            decoder_lower, model_version, model_path
        )

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
