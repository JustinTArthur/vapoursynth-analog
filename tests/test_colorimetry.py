"""Tests for the colorimetry filters: validation, prop plumbing and color math.

All clips are synthetic (BlankClip + SetFrameProps), so these run without any
capture; they only need the plugin binary installed in the venv.
"""

from __future__ import annotations

import pytest

vs = pytest.importorskip("vapoursynth")
vsanalog = pytest.importorskip("vsanalog")

core = vs.core


def _plugin_available() -> bool:
    try:
        vsanalog.plugin._ensure_plugin_loaded()
    except FileNotFoundError:
        return hasattr(vs.core, "analog")
    return True


pytestmark = pytest.mark.skipif(
    not _plugin_available(), reason="vsanalog plugin binary not installed"
)


def _yuv_clip(color, fmt=None, **props):
    fmt = fmt if fmt is not None else vs.YUV444PS
    clip = core.std.BlankClip(format=fmt, width=64, height=16, length=3, color=color)
    defaults = {"_Matrix": 6, "_Transfer": 1, "_Primaries": 6, "_Range": 0}
    defaults.update(props)
    return core.std.SetFrameProps(clip, **defaults)


def _pixel(clip, plane, n=0):
    with clip.get_frame(n) as f:
        return f[plane][0, 0]


def _props(clip):
    with clip.get_frame(0) as f:
        return dict(f.props)


# --- Python-level validation ----------------------------------------------------

def test_rejects_unknown_option_values():
    clip = _yuv_clip([0.5, 0.0, 0.0])
    with pytest.raises(ValueError, match="primaries_in_s"):
        vsanalog.modernize_chromaticity(
            clip, primaries_in_s="bt709", output_preset="hdtv"
        )
    with pytest.raises(ValueError, match="output_preset"):
        vsanalog.modernize_chromaticity(clip, output_preset="sdtv")


# --- Creation-time plugin validation --------------------------------------------

def test_requires_output_spec():
    clip = _yuv_clip([0.5, 0.0, 0.0])
    with pytest.raises(vs.Error, match="primaries_s or output_preset"):
        vsanalog.modernize_chromaticity(clip)


def test_rejects_440_with_secam_pointer():
    fmt440 = core.query_video_format(vs.YUV, vs.FLOAT, 32, 0, 1)
    clip = core.std.BlankClip(format=fmt440, width=64, height=16)
    with pytest.raises(vs.Error, match="resample_secam"):
        vsanalog.modernize_chromaticity(clip, output_preset="hdtv")
    with pytest.raises(vs.Error, match="fill_secam_by_delay"):
        vsanalog.modernize_chromaticity(clip, output_preset="hdtv")


def test_rejects_unknown_resample_filter():
    clip = _yuv_clip([0.5, 0.0, 0.0])
    with pytest.raises(ValueError, match="resample_filter_uv"):
        vsanalog.modernize_chromaticity(
            clip, output_preset="hdtv", resample_filter_uv="area"
        )


def test_rejects_gray_input():
    clip = core.std.BlankClip(format=vs.GRAYS, width=64, height=16)
    with pytest.raises(vs.Error, match="GRAY"):
        vsanalog.modernize_chromaticity(clip, output_preset="hdtv")


def test_2020cl_requires_2020_primaries():
    clip = _yuv_clip([0.5, 0.0, 0.0])
    with pytest.raises(vs.Error, match="bt2020 primaries"):
        vsanalog.modernize_chromaticity(
            clip, matrix_s="2020cl", primaries_s="p3d65", transfer_s="linear"
        )


def test_xyz_requires_rgb_matrix():
    clip = _yuv_clip([0.5, 0.0, 0.0])
    with pytest.raises(vs.Error, match="xyz"):
        vsanalog.modernize_chromaticity(
            clip, primaries_s="xyz", transfer_s="linear", matrix_s="bt709"
        )


def test_contrast_needs_bt1886():
    clip = _yuv_clip([0.5, 0.0, 0.0])
    with pytest.raises(vs.Error, match="contrast"):
        vsanalog.modernize_chromaticity(
            clip, output_preset="bt2100-pq", contrast=0.9
        )


# --- Frame-time property inference ----------------------------------------------

def test_missing_props_error_at_frame_time():
    clip = core.std.BlankClip(format=vs.YUV444PS, width=64, height=16)
    out = vsanalog.modernize_chromaticity(clip, output_preset="hdtv")
    with pytest.raises(vs.Error, match="_Primaries"):
        out.get_frame(0)


def test_in_params_override_props():
    # Nonsense _Primaries that inference would reject; the override wins.
    clip = _yuv_clip([0.5, 0.0, 0.0], _Primaries=1)
    out = vsanalog.modernize_chromaticity(
        clip, primaries_in_s="smpte-c", output_preset="hdtv"
    )
    out.get_frame(0)  # No error


# --- Output format and tagging --------------------------------------------------

def test_hdtv_preset_props_and_format():
    out = vsanalog.modernize_chromaticity(
        _yuv_clip([0.5, 0.0, 0.0]), output_preset="hdtv"
    )
    assert out.format.id == vs.YUV444PS
    props = _props(out)
    assert props["_Matrix"] == 1
    assert props["_Transfer"] == 1
    assert props["_Primaries"] == 1
    assert props["_Range"] == 0  # Studio range for Y'CbCr output
    assert "_ChromaLocation" not in props


def test_rgb_output():
    out = vsanalog.modernize_chromaticity(
        _yuv_clip([0.5, 0.0, 0.0]), output_preset="hdtv", matrix_s="rgb"
    )
    assert out.format.color_family == vs.RGB
    props = _props(out)
    assert props["_Matrix"] == 0
    assert props["_Range"] == 1  # RGB defaults to full range


def test_bt2100_pq_preset_tags():
    out = vsanalog.modernize_chromaticity(
        _yuv_clip([0.5, 0.0, 0.0]), output_preset="bt2100-pq"
    )
    props = _props(out)
    assert props["_Matrix"] == 9
    assert props["_Transfer"] == 16
    assert props["_Primaries"] == 9


def test_bt2100_hlg_preset_tags():
    out = vsanalog.modernize_chromaticity(
        _yuv_clip([0.5, 0.0, 0.0]), output_preset="bt2100-hlg"
    )
    props = _props(out)
    assert props["_Matrix"] == 9
    assert props["_Transfer"] == 18
    assert props["_Primaries"] == 9


def test_uhdtv_alias_is_pq():
    out = vsanalog.modernize_chromaticity(
        _yuv_clip([0.5, 0.0, 0.0]), output_preset="uhdtv"
    )
    assert _props(out)["_Transfer"] == 16


def test_bt2020_sdr_transfer_tagged_as_2020():
    out = vsanalog.modernize_chromaticity(
        _yuv_clip([0.5, 0.0, 0.0]), output_preset="bt2020-sdr"
    )
    assert _props(out)["_Transfer"] == 15  # Float output takes the 12-bit tag


def test_srgb_preset_is_rgb():
    # IEC 61966-2-1 defines no color-difference form, so the preset carries no
    # matrix and lands on RGB.
    out = vsanalog.modernize_chromaticity(
        _yuv_clip([0.5, 0.0, 0.0]), output_preset="srgb"
    )
    assert out.format.color_family == vs.RGB
    props = _props(out)
    assert props["_Matrix"] == 0
    assert props["_Transfer"] == 13
    assert props["_Primaries"] == 1
    assert props["_Range"] == 1


def test_srgb_preset_accepts_a_matrix():
    out = vsanalog.modernize_chromaticity(
        _yuv_clip([0.5, 0.0, 0.0]), output_preset="srgb", matrix_s="bt709"
    )
    assert out.format.id == vs.YUV444PS
    assert _props(out)["_Transfer"] == 13


def test_2020cl_tags():
    out = vsanalog.modernize_chromaticity(
        _yuv_clip([0.5, 0.0, 0.0]), matrix_s="2020cl"
    )
    props = _props(out)
    assert props["_Matrix"] == 10
    assert props["_Primaries"] == 9
    assert props["_Transfer"] == 15  # Falls back to the BT.2020 OETF


def test_cl_pairs_with_any_transfer():
    # H.273 E-62..E-65 derive the CL normalizers from whichever transfer is
    # signalled, so the matrix and the curve are independent axes.
    clip = _yuv_clip([0.5, 0.1, -0.05])
    for transfer, tag in (("pq", 16), ("hlg", 18), ("linear", 8),
                          ("bt1886-annex-1", 15)):
        out = vsanalog.modernize_chromaticity(
            clip, matrix_s="2020cl", transfer_s=transfer
        )
        props = _props(out)
        assert props["_Matrix"] == 10
        assert props["_Transfer"] == tag
    # Different curves must give different normalizers, hence different chroma.
    pq = vsanalog.modernize_chromaticity(clip, matrix_s="2020cl", transfer_s="pq")
    oetf = vsanalog.modernize_chromaticity(clip, matrix_s="2020cl")
    assert _pixel(pq, 1) != pytest.approx(_pixel(oetf, 1), abs=1e-4)


def test_cl_equals_ncl_under_linear_transfer():
    # With an identity curve the prime operator vanishes: E-62..E-65 collapse
    # to N=P=1-K, and CL's equations become the NCL matrix exactly.
    clip = _yuv_clip([0.5, 0.1, -0.05])
    cl = vsanalog.modernize_chromaticity(clip, matrix_s="2020cl", transfer_s="linear")
    ncl = vsanalog.modernize_chromaticity(
        clip, matrix_s="bt2020ncl", primaries_s="bt2020", transfer_s="linear"
    )
    for plane in range(3):
        assert _pixel(cl, plane) == pytest.approx(_pixel(ncl, plane), abs=1e-7)


def test_chromaticity_derived_cl():
    clip = _yuv_clip([0.5, 0.1, -0.05])
    out = vsanalog.modernize_chromaticity(
        clip, matrix_s="chromacl", primaries_s="p3d65", transfer_s="linear"
    )
    props = _props(out)
    assert props["_Matrix"] == 13
    assert props["_Primaries"] == 12

    # Against BT.2020 primaries the derived weights are BT.2020's own, so
    # chromacl and 2020cl must agree exactly.
    derived = vsanalog.modernize_chromaticity(
        clip, matrix_s="chromacl", primaries_s="bt2020", transfer_s="linear"
    )
    named = vsanalog.modernize_chromaticity(
        clip, matrix_s="2020cl", transfer_s="linear"
    )
    for plane in range(3):
        assert _pixel(derived, plane) == pytest.approx(_pixel(named, plane), abs=1e-6)


# --- Color math ----------------------------------------------------------------

def test_white_maps_to_white():
    # SMPTE C and BT.709 share the D65 white, so reference white must land on
    # reference white without any chromatic adaptation.
    out = vsanalog.modernize_chromaticity(
        _yuv_clip([1.0, 0.0, 0.0]), output_preset="hdtv"
    )
    assert _pixel(out, 0) == pytest.approx(1.0, abs=1e-4)
    assert _pixel(out, 1) == pytest.approx(0.0, abs=1e-4)
    assert _pixel(out, 2) == pytest.approx(0.0, abs=1e-4)


def test_achromatic_stays_achromatic_mid_gray():
    out = vsanalog.modernize_chromaticity(
        _yuv_clip([0.5, 0.0, 0.0]), output_preset="hdtv"
    )
    assert _pixel(out, 1) == pytest.approx(0.0, abs=1e-4)
    assert _pixel(out, 2) == pytest.approx(0.0, abs=1e-4)
    # Same D65 white and pure-power in/out curves: gray passes through changed
    # only by the primaries matrix, which is identity-adjacent here.
    assert 0.4 < _pixel(out, 0) < 0.6


def test_d93_white_tints_without_adaptation():
    japan = _yuv_clip([1.0, 0.0, 0.0], _Primaries=6)
    out = vsanalog.modernize_chromaticity(
        japan, primaries_in_s="bt1700-japan", output_preset="hdtv"
    )
    # A D93 white shown on a D65 display reads blue: chroma well off neutral.
    assert abs(_pixel(out, 1)) + abs(_pixel(out, 2)) > 0.01

    adapted = vsanalog.modernize_chromaticity(
        japan, primaries_in_s="bt1700-japan", output_preset="hdtv",
        chromatic_adaptation=True,
    )
    assert _pixel(adapted, 1) == pytest.approx(0.0, abs=1e-4)
    assert _pixel(adapted, 2) == pytest.approx(0.0, abs=1e-4)
    assert _pixel(adapted, 0) == pytest.approx(1.0, abs=1e-3)


def test_pq_reference_white():
    out = vsanalog.modernize_chromaticity(
        _yuv_clip([1.0, 0.0, 0.0]), output_preset="bt2100-pq"
    )
    # PQ code for 100 cd/m² (the nominal_luminance default).
    assert _pixel(out, 0) == pytest.approx(0.50808, abs=1e-4)


def test_nominal_luminance_scales_pq():
    out = vsanalog.modernize_chromaticity(
        _yuv_clip([1.0, 0.0, 0.0]), output_preset="bt2100-pq", nominal_luminance=203.0
    )
    # BT.2408's 203 cd/m² graphics white sits at PQ ~0.58.
    assert _pixel(out, 0) == pytest.approx(0.5806, abs=1e-3)


def test_srgb_curve_matches_the_standard():
    # Checked against the plugin's own linear output so only the curve is under
    # test: IEC 61966-2-1 Equations 7 and 8, both segments.
    def linear_and_srgb(color):
        clip = _yuv_clip(color, _Transfer=8)
        lin = vsanalog.modernize_chromaticity(
            clip, primaries_s="bt709", transfer_s="linear", matrix_s="rgb"
        )
        return _pixel(lin, 0), _pixel(
            vsanalog.modernize_chromaticity(clip, output_preset="srgb"), 0
        )

    lin, out = linear_and_srgb([0.5, 0.0, 0.0])
    assert lin > 0.0031308
    assert out == pytest.approx(1.055 * lin ** (1 / 2.4) - 0.055, abs=1e-6)

    lin, out = linear_and_srgb([0.002, 0.0, 0.0])
    assert 0.0 < lin <= 0.0031308
    assert out == pytest.approx(12.92 * lin, abs=1e-6)


def test_integer_16bit_roundtrip_white():
    clip = _yuv_clip([60160, 32768, 32768], fmt=vs.YUV444P16)
    out = vsanalog.modernize_chromaticity(clip, output_preset="hdtv")
    assert out.format.id == vs.YUV444P16
    assert _pixel(out, 0) == pytest.approx(60160, abs=2)
    assert _pixel(out, 1) == pytest.approx(32768, abs=2)


def test_subsampled_round_trip():
    # 4:2:0 in, same subsampling out; a uniform clip is invariant to the
    # kernel, so the result must match the 4:4:4 path exactly.
    clip420 = _yuv_clip([0.5, 0.1, -0.05], fmt=vs.YUV420PS, _ChromaLocation=0)
    out = vsanalog.modernize_chromaticity(clip420, output_preset="hdtv")
    assert out.format.id == vs.YUV420PS
    props = _props(out)
    assert props["_Matrix"] == 1
    assert props["_ChromaLocation"] == 0  # Preserved for the re-siting

    clip444 = _yuv_clip([0.5, 0.1, -0.05])
    ref = vsanalog.modernize_chromaticity(clip444, output_preset="hdtv")
    for plane in range(3):
        assert _pixel(out, plane) == pytest.approx(_pixel(ref, plane), abs=1e-6)


def test_subsampled_to_rgb_stays_full_res():
    clip420 = _yuv_clip([0.5, 0.1, -0.05], fmt=vs.YUV420PS)
    out = vsanalog.modernize_chromaticity(
        clip420, output_preset="hdtv", matrix_s="rgb"
    )
    assert out.format.color_family == vs.RGB
    assert out.format.subsampling_w == 0 and out.format.subsampling_h == 0
    assert "_ChromaLocation" not in _props(out)


def test_transfer_6_infers_bt1886():
    # ST 170's own EOTF saw little real display use; _Transfer=6 deliberately
    # reads as BT.1886, identically to an explicit override.
    colored = _yuv_clip([0.5, 0.1, -0.05], _Transfer=6)
    inferred = vsanalog.modernize_chromaticity(colored, output_preset="hdtv")
    explicit = vsanalog.modernize_chromaticity(
        colored, transfer_in_s="bt1886-annex-1", output_preset="hdtv"
    )
    literal = vsanalog.modernize_chromaticity(
        colored, transfer_in_s="st170-display", output_preset="hdtv"
    )
    assert _pixel(inferred, 0) == _pixel(explicit, 0)
    assert _pixel(inferred, 0) != pytest.approx(_pixel(literal, 0), abs=1e-4)


def test_classic_matrix_decodes_differently():
    colored = _yuv_clip([0.5, 0.1, -0.1])
    modern = vsanalog.modernize_chromaticity(
        colored, matrix_in_s="analog-modern", output_preset="hdtv", matrix_s="rgb"
    )
    classic = vsanalog.modernize_chromaticity(
        colored, matrix_in_s="analog-classic", output_preset="hdtv", matrix_s="rgb"
    )
    assert _pixel(modern, 0) != pytest.approx(_pixel(classic, 0), abs=1e-4)


# ================================================================================
# amplify_chroma
# ================================================================================

# --- Validation -----------------------------------------------------------------

def test_amplify_rejects_negative_gain():
    clip = _yuv_clip([0.5, 0.1, -0.2])
    with pytest.raises(ValueError, match="gain"):
        vsanalog.amplify_chroma(clip, -1.0)


def test_amplify_rejects_unknown_resample_filter():
    clip = _yuv_clip([0.5, 0.1, -0.2])
    with pytest.raises(ValueError, match="resample_filter_uv"):
        vsanalog.amplify_chroma(clip, 1.5, resample_filter_uv="area")


def test_amplify_rejects_gray_input():
    clip = core.std.BlankClip(format=vs.GRAYS, width=64, height=16)
    with pytest.raises(vs.Error, match="GRAY"):
        vsanalog.amplify_chroma(clip, 1.5)


def test_amplify_unity_gain_passes_through():
    clip = _yuv_clip([32768, 45000, 20000], fmt=vs.YUV444P16, _Matrix=1)
    out = vsanalog.amplify_chroma(clip, 1.0)
    # Not even a conversion round trip, which could only cost precision.
    assert _pixel(out, 1) == 45000


def test_amplify_needs_a_matrix_at_frame_time():
    clip = core.std.BlankClip(format=vs.YUV444PS, width=64, height=16)
    out = vsanalog.amplify_chroma(clip, 1.5)
    with pytest.raises(vs.Error, match="_Matrix"):
        out.get_frame(0)


# --- The in-place path (float Y'CbCr already on the analog axes) -----------------

@pytest.mark.parametrize("matrix", [4, 5, 6])
def test_amplify_scales_analog_chroma_in_place(matrix):
    clip = _yuv_clip([0.5, 0.1, -0.2], _Matrix=matrix)
    out = vsanalog.amplify_chroma(clip, 1.5)
    assert out.format.id == vs.YUV444PS
    assert _pixel(out, 0) == 0.5  # Luma untouched, bit for bit
    assert _pixel(out, 1) == pytest.approx(0.15, abs=1e-7)
    assert _pixel(out, 2) == pytest.approx(-0.3, abs=1e-7)
    assert _props(out)["_Matrix"] == matrix


@pytest.mark.parametrize("primaries", [4, 5, 6, 7, 2])
def test_amplify_accepts_analog_primaries(primaries):
    out = vsanalog.amplify_chroma(_yuv_clip([0.5, 0.1, -0.2], _Primaries=primaries), 1.5)
    assert _pixel(out, 1) == pytest.approx(0.15, abs=1e-6)


def test_amplify_accepts_untagged_primaries():
    clip = core.std.BlankClip(format=vs.YUV444PS, width=64, height=16,
                              color=[0.5, 0.1, -0.2])
    out = vsanalog.amplify_chroma(core.std.SetFrameProps(clip, _Matrix=6), 1.5)
    assert _pixel(out, 1) == pytest.approx(0.15, abs=1e-6)


@pytest.mark.parametrize("primaries", [1, 9, 11])
def test_amplify_rejects_modern_primaries(primaries):
    # Nothing ever split luma from chroma with the analog coefficients on
    # these, so the gain would be describing a signal that never existed.
    out = vsanalog.amplify_chroma(_yuv_clip([0.5, 0.1, -0.2], _Primaries=primaries), 1.5)
    with pytest.raises(vs.Error, match="_Primaries"):
        out.get_frame(0)


def test_amplify_rejects_modern_primaries_on_rgb():
    clip = core.std.BlankClip(format=vs.RGBS, width=64, height=16, color=[0.6, 0.3, 0.3])
    out = vsanalog.amplify_chroma(core.std.SetFrameProps(clip, _Primaries=1), 1.5)
    with pytest.raises(vs.Error, match="_Primaries"):
        out.get_frame(0)


def test_amplify_zero_gain_is_monochrome():
    out = vsanalog.amplify_chroma(_yuv_clip([0.5, 0.1, -0.2]), 0.0)
    assert _pixel(out, 1) == 0.0
    assert _pixel(out, 2) == 0.0


def test_amplify_accepts_the_440_lattice_in_place():
    # SECAM's line-sequential 4:4:0 needs no resampling for a per-sample gain,
    # so it goes through untouched by the restriction the round trip carries.
    fmt440 = core.query_video_format(vs.YUV, vs.FLOAT, 32, 0, 1)
    out = vsanalog.amplify_chroma(_yuv_clip([0.5, 0.1, -0.2], fmt=fmt440), 2.0)
    assert _pixel(out, 1) == pytest.approx(0.2, abs=1e-7)


@pytest.mark.parametrize("matrix", [4, 5, 6])
def test_amplify_integer_440_converts_without_touching_the_lattice(matrix):
    # A depth-only trip through YUV440PS names no matrix, so resize converts
    # the samples and nothing else — the line-sequential lattice survives.
    fmt440 = core.query_video_format(vs.YUV, vs.INTEGER, 16, 0, 1)
    out = vsanalog.amplify_chroma(
        _yuv_clip([32768, 45000, 20000], fmt=fmt440, _Matrix=matrix), 1.5
    )
    assert out.format.id == fmt440.id
    assert _pixel(out, 0) == 32768
    assert _pixel(out, 1) == pytest.approx(32768 + 1.5 * (45000 - 32768), abs=2)


def test_amplify_rejects_440_needing_a_matrix_change():
    # Moving the color differences onto the analog axes is the one thing that
    # would resample this clip's chroma, so those frames are refused instead.
    for fmt in (core.query_video_format(vs.YUV, vs.FLOAT, 32, 0, 1),
                core.query_video_format(vs.YUV, vs.INTEGER, 16, 0, 1)):
        color = [0.5, 0.1, -0.2] if fmt.sample_type == vs.FLOAT else [32768, 45000, 20000]
        out = vsanalog.amplify_chroma(_yuv_clip(color, fmt=fmt, _Matrix=1), 2.0)
        with pytest.raises(vs.Error, match="resample_secam"):
            out.get_frame(0)


# --- The conversion round trip ---------------------------------------------------

def test_amplify_round_trip_keeps_format_and_tagging():
    clip = _yuv_clip([32768, 45000, 20000], fmt=vs.YUV420P16, _Matrix=1, _ChromaLocation=0)
    out = vsanalog.amplify_chroma(clip, 1.5)
    assert out.format.id == vs.YUV420P16
    props = _props(out)
    assert props["_Matrix"] == 1
    assert props["_ChromaLocation"] == 0


def test_amplify_round_trip_scales_chroma_exactly():
    # Converting through Matrix 6 holds the *analog* luma constant, so a BT.709
    # frame keeps 1.5x chroma on its own axes and has its Y' re-derived.
    clip = _yuv_clip([0.5, 0.1, -0.2], _Matrix=1)
    out = vsanalog.amplify_chroma(clip, 1.5)
    assert _pixel(out, 1) == pytest.approx(0.15, abs=1e-6)
    assert _pixel(out, 2) == pytest.approx(-0.3, abs=1e-6)
    assert _pixel(out, 0) != pytest.approx(0.5, abs=1e-3)


def test_amplify_round_trip_leaves_neutrals_neutral():
    out = vsanalog.amplify_chroma(_yuv_clip([0.5, 0.0, 0.0], _Matrix=1), 2.0)
    assert _pixel(out, 0) == pytest.approx(0.5, abs=1e-5)
    assert _pixel(out, 1) == pytest.approx(0.0, abs=1e-6)
    assert _pixel(out, 2) == pytest.approx(0.0, abs=1e-6)


def test_amplify_integer_analog_matrix_round_trips():
    # Already analog color differences, but integer, so it still converts:
    # a pure depth change, landing on the arithmetic answer.
    clip = _yuv_clip([32768, 45000, 20000], fmt=vs.YUV444P16)
    out = vsanalog.amplify_chroma(clip, 1.5)
    assert out.format.id == vs.YUV444P16
    assert _pixel(out, 0) == pytest.approx(32768, abs=2)
    assert _pixel(out, 1) == pytest.approx(32768 + 1.5 * (45000 - 32768), abs=2)
    assert _pixel(out, 2) == pytest.approx(32768 - 1.5 * (32768 - 20000), abs=2)


@pytest.mark.parametrize("matrix", [4, 5, 6])
def test_amplify_never_resamples_analog_chroma(matrix):
    """A subsampled integer clip keeps its chroma detail exactly.

    Asking resize for the Matrix 6 axes would be a real color-difference
    change for code 4, sending its chroma through the uv kernel at 4:4:4 and
    softening it for a gain that needs no such thing. Naming no matrix at all
    avoids that for all three analog codes.
    """
    left = _yuv_clip([128, 160, 90], fmt=vs.YUV420P8, _Matrix=matrix)
    right = _yuv_clip([128, 96, 200], fmt=vs.YUV420P8, _Matrix=matrix)
    edge = core.std.SetFrameProps(
        core.std.StackHorizontal([left, right]), _Matrix=matrix, _Range=0
    )
    out = vsanalog.amplify_chroma(edge, 1.5)

    with out.get_frame(0) as f:
        width = f[1].shape[1]
        across = {f[1][0, x] for x in range(width // 2 - 2, width // 2 + 2)}
    # Two values, no blend between them: 128 + 1.5*32 and 128 - 1.5*32.
    assert across == {176, 80}


def test_amplify_rgb_round_trip():
    clip = core.std.BlankClip(format=vs.RGBS, width=64, height=16, color=[0.6, 0.3, 0.3])
    out = vsanalog.amplify_chroma(clip, 1.5)
    assert out.format.id == vs.RGBS
    assert _pixel(out, 0) > 0.6  # Red pushed further from the achromatic axis
    assert _pixel(out, 1) < 0.3

    gray = core.std.BlankClip(format=vs.RGBS, width=64, height=16, color=[0.4] * 3)
    neutral = vsanalog.amplify_chroma(gray, 2.0)
    for plane in range(3):
        assert _pixel(neutral, plane) == pytest.approx(0.4, abs=1e-6)


def test_amplify_selects_the_path_per_frame():
    # _Matrix can change frame to frame; each frame takes its own route.
    analog = _yuv_clip([0.5, 0.1, -0.2], _Matrix=6)
    hd = _yuv_clip([0.5, 0.1, -0.2], _Matrix=1)
    out = vsanalog.amplify_chroma(core.std.Interleave([analog, hd]), 1.5)
    assert _pixel(out, 0, 0) == 0.5                            # In place
    assert _pixel(out, 0, 1) != pytest.approx(0.5, abs=1e-3)   # Round trip
    with out.get_frame(0) as f0, out.get_frame(1) as f1:
        assert f0.props["_Matrix"] == 6
        assert f1.props["_Matrix"] == 1
