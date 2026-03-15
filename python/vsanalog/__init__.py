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


@requires_plugin
def decode_4fsc_video(
    composite_or_luma_source: str | Path,
    chroma_or_pb_source: str | Path | None = None,
    pr_source: str | Path | None = None,
    *,
    decoder: str | None = None,
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
    """Decode 4FSC (four times subcarrier) digitized analog video.

    Reads time-base corrected (TBC) captures produced by ld-decode or vhs-decode
    and returns a VapourSynth clip with decoded video.

    Args:
        composite_or_luma_source: Path to the composite or luma-only .tbc file.
        chroma_or_pb_source: Path to a separate chroma .tbc file, used for
            Y/C-separated sources like S-Video or VHS color-under.
        pr_source: Path to the Pr component .tbc file (component video).
        decoder: Chroma decoder to use. One of ``"ntsc1d"``, ``"ntsc2d"``,
            ``"ntsc3d"``, ``"ntsc3dnoadapt"``, ``"pal2d"``, ``"transform2d"``,
            ``"transform3d"``, or ``"mono"``. When *None*, the decoder is
            chosen automatically based on the video system.
        reverse_fields: Swap field order.
        chroma_gain: Chroma gain multiplier for saturation adjustment.
        chroma_phase: Chroma phase adjustment in degrees.
        chroma_nr: Chroma noise-reduction level (NTSC decoders only).
        luma_nr: Luma noise-reduction level.
        phase_compensation: Enable NTSC phase compensation.
        padding_multiple: Round output dimensions to this multiple.
            Set to 0 to disable padding.
        dropout_correct: Enable dropout correction.
        dropout_overcorrect: Extend dropout boundaries by ±24 samples
            (for heavily damaged sources).
        dropout_intra: Force intra-field-only dropout correction.
        dropout_composite_or_luma_extra_sources: Additional composite/luma
            .tbc files for multi-source dropout correction.
        dropout_chroma_extra_sources: Additional chroma .tbc files for
            multi-source dropout correction.
        fpsnum: Override frame-rate numerator.
        fpsden: Override frame-rate denominator (used with *fpsnum*).

    Returns:
        A VapourSynth clip in YUV444PS or GRAYS format (32-bit float).
    """
    kwargs: dict[str, Any] = {}

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
