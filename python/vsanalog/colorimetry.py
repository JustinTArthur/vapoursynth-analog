"""Colorimetry and photometry functions for analog-era video."""

from __future__ import annotations

from typing import Any

import vapoursynth as vs

from .plugin import requires_plugin

__all__ = ["amplify_chroma", "modernize_chromaticity"]

# Alias sets accepted by the plugin, validated here so mistakes raise a Python
# ValueError with the full menu instead of a filter-creation error.
_PRIMARIES_IN = {
    "ntsc-1953", "bt470m", "470m", "fcc",
    "bt470-japan", "470m93", "ntscj",
    "bt1700-japan", "170j",
    "pal", "ebu", "bbc", "470bg",
    "smpte-c", "st170", "170m",
    "code-point-22",
    "ecia-xxa", "p22", "ecia-xxb", "ecia-xxc", "ecia-xxd",
    "ecia-xxe", "ecia-xxf", "ecia-xxg",
    "rca-sulfide-8500k", "rca-sulfide-9300k-27mpcd", "rca-sulfide-c",
    "rca-p22-4-67", "rca-p22-5-61", "rca-p22-9-65",
    "sony-p22", "studio-japan", "nederland-proposal",
}
_TRANSFER_IN = {
    "linear",
    "ntsc-1953", "bt470m", "470m", "fcc", "gamma22",
    "bt470bg", "470bg", "tube", "gamma28",
    "st170-scene", "st170-oetf", "bt601", "601",
    "st170-display", "st170-eotf",
    "bt1886-annex-1", "1886", "lcd", "gamma24",
    "bt1886-appendix-1", "1886a", "crt",
    "srgb", "iec-61966-2-1",
}
_MATRIX_IN = {
    "analog-classic", "ntsc-1953", "fcc",
    "analog-modern", "bt470", "bt1700", "st170", "170m", "bt601", "601",
}
_PRIMARIES_OUT = {
    "bt709", "709", "bt2020", "2020",
    "p3dci", "st431-2", "p3d65", "st432-1", "xyz", "st428",
}
_TRANSFER_OUT = {
    "linear",
    "bt1886-annex-1", "1886", "lcd", "gamma24",
    "srgb", "iec-61966-2-1",
    "pq", "st2084", "2084",
    "hlg", "std-b67",
}
_MATRIX_OUT = {
    "rgb", "bt709", "709",
    "bt2020ncl", "bt2100", "2020ncl", "2020", "2100",
    "2020cl", "chromacl", "chromaticity-derived-cl",
}
_OUTPUT_PRESETS = {
    "hdtv", "bt709", "uhdtv", "bt2100-pq", "bt2100-hlg", "bt2020-sdr",
    "srgb", "iec-61966-2-1",
}
_RESAMPLE_FILTERS = {
    "point", "bilinear", "bicubic", "spline16", "spline36", "spline64", "lanczos",
}


def _validate_choice(name: str, value: str | None, allowed: set[str]) -> str | None:
    if value is None:
        return None
    lowered = value.lower()
    if lowered not in allowed:
        raise ValueError(
            f"{name} must be one of {sorted(allowed)}, got {value!r}"
        )
    return lowered


@requires_plugin
def amplify_chroma(
    clip: vs.VideoNode,
    gain: float,
    *,
    resample_filter_uv: str | None = None,
    filter_param_a_uv: float | None = None,
    filter_param_b_uv: float | None = None,
) -> vs.VideoNode:
    """Amplify or attenuate the color-difference signals.

    The post-decode counterpart of :func:`decode_4fsc_video`'s *chroma_gain*,
    which scales the demodulated color differences on their way out of the
    decoder. This does the same thing to a clip that already exists, so it
    works just as well on a conventional capture loaded through a source
    plugin such as BestSource as it does on a fresh decode.

    Think of it as a saturation control, but saturation as the analog video
    domain defines it — a gain on E'Cb/E'Cr — rather than the saturation axis
    of an HSV or HLS model. ``gain`` above 1.0 amplifies, below 1.0
    attenuates, ``0.0`` leaves a monochrome picture, and ``1.0`` hands the
    clip straight back untouched.

    Frames that already carry E'Y E'Cb E'Cr — 32-bit float Y'CbCr tagged
    ``_Matrix=4`` (NTSC-1953), ``_Matrix=5`` (BT.470 BG) or ``_Matrix=6``
    (SMPTE ST 170), as :func:`decode_4fsc_video` emits — are scaled in place,
    luma and chroma siting untouched. All three are the same luma/chroma
    split, the coefficients NTSC-1953 derived from its primaries at
    Illuminant C, code 4 at its original precision and codes 5 and 6 at the
    higher one later systems restated it to (keeping the coefficients even
    though their primaries and white had moved — a mismatch those signals
    were built with regardless).

    Frames carrying those same color differences in another format — integer
    Y'CbCr of any depth — go through a float intermediate of the same
    subsampling and back. That conversion names no matrix, so ``resize``
    converts the samples and nothing else: no chroma is resampled, whichever
    analog matrix the frame carries.

    Frames on other axes take the same trip, but to a ``_Matrix=6``
    intermediate, which is a real color-difference change and so does resample
    the chroma of a subsampled clip (see *resample_filter_uv*). RGB always
    takes that path, having no color differences of its own.

    On a frame whose matrix isn't an analog one, the round trip holds the
    *analog* luma constant, which is what the decoder's own gain does. Its
    own Y' therefore shifts a little as the color is scaled — a BT.709 frame
    taken to ``gain=0.0`` lands on the analog luma of its colors, not on its
    BT.709 one. The chroma comes out scaled by exactly ``gain`` either way.

    Only analog-era colorimetry is accepted: frames tagged ``_Primaries`` 4
    (NTSC-1953), 5 (EBU), 6 (SMPTE ST 170) or 7 (SMPTE ST 240), or carrying
    no tag or ``unspecified`` (2). Newer primaries are rejected — no signal
    was ever built by splitting luma from chroma with the analog coefficients
    on them, and there is no telling what such a picture was originally
    broadcast in. Amplify before :func:`modernize_chromaticity` converts the
    picture, not after.

    Integer output is rounded without dithering, and the conversion is only
    as precise as the format allows, so prefer high-bit-depth material.
    Nothing is clipped in float; integer formats clamp at their bounds, so
    amplifying a saturated picture can flatten the most colorful areas.

    """
    if gain < 0:
        raise ValueError(f"gain must be 0.0 or greater, got {gain!r}")

    kwargs: dict[str, Any] = {}
    lowered = _validate_choice("resample_filter_uv", resample_filter_uv, _RESAMPLE_FILTERS)
    if lowered is not None:
        kwargs["resample_filter_uv"] = lowered
    for name, value in (
        ("filter_param_a_uv", filter_param_a_uv),
        ("filter_param_b_uv", filter_param_b_uv),
    ):
        if value is not None:
            kwargs[name] = value

    return vs.core.analog.amplify_chroma(clip, gain=gain, **kwargs)


@requires_plugin
def modernize_chromaticity(
    clip: vs.VideoNode,
    *,
    primaries_in_s: str | None = None,
    transfer_in_s: str | None = None,
    matrix_in_s: str | None = None,
    primaries_s: str | None = None,
    transfer_s: str | None = None,
    matrix_s: str | None = None,
    output_preset: str | None = None,
    resample_filter_uv: str | None = None,
    filter_param_a_uv: float | None = None,
    filter_param_b_uv: float | None = None,
    chromatic_adaptation: bool = False,
    nominal_luminance: float | None = None,
    contrast_in: float | None = None,
    brightness_in: float | None = None,
    contrast: float | None = None,
    brightness: float | None = None,
) -> vs.VideoNode:
    """Convert analog-era colorimetry and photometry to a modern target.

    Meant downstream of :func:`decode_4fsc_video`, or after loading previously
    captured video through a source plugin such as BestSource. Color only:
    no geometry conversions and no dithering. The color math runs at 4:4:4;
    subsampled Y'CbCr input is upsampled through the ``resize`` plugin for the
    conversion and returned to its original subsampling on output (see
    *resample_filter_uv*). Feed it high-bit-depth (ideally 32-bit float)
    material; integer output is rounded without dithering.

    Parameter names mirror VapourSynth's ``resize`` filters where the meaning
    is similar, but unlike ``resize`` the ``*_in`` parameters *override* frame
    properties. When an ``*_in`` parameter is omitted the value is inferred
    from the matching frame property, and the filter errors if neither is
    available.

    Output range follows the output matrix's convention (studio range for
    Y'CbCr, full range for RGB); input range is read from the ``_ColorRange``
    frame property with the same convention as the fallback.
    """
    kwargs: dict[str, Any] = {}
    for name, value, allowed in (
        ("primaries_in_s", primaries_in_s, _PRIMARIES_IN),
        ("transfer_in_s", transfer_in_s, _TRANSFER_IN),
        ("matrix_in_s", matrix_in_s, _MATRIX_IN),
        ("primaries_s", primaries_s, _PRIMARIES_OUT),
        ("transfer_s", transfer_s, _TRANSFER_OUT),
        ("matrix_s", matrix_s, _MATRIX_OUT),
        ("output_preset", output_preset, _OUTPUT_PRESETS),
        ("resample_filter_uv", resample_filter_uv, _RESAMPLE_FILTERS),
    ):
        lowered = _validate_choice(name, value, allowed)
        if lowered is not None:
            kwargs[name] = lowered

    for name, value in (
        ("filter_param_a_uv", filter_param_a_uv),
        ("filter_param_b_uv", filter_param_b_uv),
        ("nominal_luminance", nominal_luminance),
        ("contrast_in", contrast_in),
        ("brightness_in", brightness_in),
        ("contrast", contrast),
        ("brightness", brightness),
    ):
        if value is not None:
            kwargs[name] = value

    return vs.core.analog.modernize_chromaticity(
        clip,
        chromatic_adaptation=chromatic_adaptation,
        **kwargs,
    )
