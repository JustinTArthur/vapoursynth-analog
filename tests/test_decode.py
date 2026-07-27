"""Integration decode tests. Skipped unless a capture is provided via env vars
(see conftest.py). They validate output format and frame properties end-to-end.
"""

from __future__ import annotations

import pytest

vs = pytest.importorskip("vapoursynth")
vsanalog = pytest.importorskip("vsanalog")


def _first_frame(clip):
    with clip.get_frame(0) as f:
        return dict(f.props)


def test_ntsc_default_yuv444(ntsc_tbc):
    clip = vsanalog.decode_4fsc_video(str(ntsc_tbc))
    assert clip.format.name == "YUV444PS"
    props = _first_frame(clip)
    assert props["_Matrix"] == 6  # SMPTE ST 170
    assert "AnalogFirstActiveSample" in props
    assert "AnalogFirstActiveLine" in props


def test_ntsc_rgb(ntsc_tbc):
    clip = vsanalog.decode_4fsc_video(str(ntsc_tbc), color_family="rgb")
    assert clip.format.name == "RGBS"
    assert _first_frame(clip)["_Matrix"] == 0  # identity for RGB


def test_ntsc_gray(ntsc_tbc):
    clip = vsanalog.decode_4fsc_video(str(ntsc_tbc), color_family="gray")
    assert clip.format.name == "GrayS"


def test_pal_default(pal_tbc):
    clip = vsanalog.decode_4fsc_video(str(pal_tbc))
    assert clip.format.name == "YUV444PS"
    assert _first_frame(clip)["_Matrix"] == 5  # BT.470 BG


def test_ntsc_standard_window(ntsc_tbc):
    # ST 244's digital active line over ST 170's active picture, reported in
    # those standards' own numbering. Field 2's first active line (283) sits
    # half a line above field 1's, so it is the topmost line and the frame is
    # bottom field first.
    clip = vsanalog.decode_4fsc_video(str(ntsc_tbc))
    assert (clip.width, clip.height) == (768, 486)
    props = _first_frame(clip)
    assert props["AnalogFirstActiveSample"] == 0
    assert props["AnalogLastActiveSample"] == 767
    assert props["AnalogFirstActiveLine"] == 283
    assert props["AnalogLastActiveLine"] == 263
    assert props["_FieldBased"] == 1


def test_pal_standard_window(pal_tbc):
    # EBU Tech 3280-E's digital active line over BT.1700's active picture.
    clip = vsanalog.decode_4fsc_video(str(pal_tbc))
    assert (clip.width, clip.height) == (948, 576)
    props = _first_frame(clip)
    assert props["AnalogFirstActiveSample"] == 0
    assert props["AnalogLastActiveSample"] == 947
    assert props["AnalogFirstActiveLine"] == 23
    assert props["AnalogLastActiveLine"] == 623
    assert props["_FieldBased"] == 2


def test_negative_first_sample_reaches_into_blanking(ntsc_tbc):
    # Sample -125 is 0H on a 525-line source, so the window grows by exactly
    # the 125 samples of line blanking that precede the digital active line.
    clip = vsanalog.decode_4fsc_video(
        str(ntsc_tbc), decoder="mono", color_family="gray", first_active_sample=-125,
    )
    assert clip.width == 768 + 125
    assert _first_frame(clip)["AnalogFirstActiveSample"] == 785


def test_upside_down_line_range_rejected(ntsc_tbc):
    # 263 is a field 1 line and 283 a field 2 line, so this names the standard
    # window's bottom line first. The numbering is not monotonic down the
    # raster, so the bounds have to be checked after weaving.
    with pytest.raises(vs.Error):
        vsanalog.decode_4fsc_video(
            str(ntsc_tbc), first_active_line=263, last_active_line=283,
        )


def test_precision_and_filter_passthrough(ntsc_tbc):
    clip = vsanalog.decode_4fsc_video(
        str(ntsc_tbc),
        chroma_filter="equiband",
        color_difference_precision="classic",
        broadcast_scaling_precision="modern",
    )
    _first_frame(clip)  # decodes without error


def test_secam_440(secam_yc):
    luma, chroma = secam_yc
    clip = vsanalog.decode_4fsc_video(str(luma), str(chroma), decoder="secam")
    assert clip.format.name == "YUV440PS"
    with clip.get_frame(0) as f:
        props = dict(f.props)
        # Chroma should sit near neutral, not railed to the extremes (a
        # luma-only SECAM decode rails U/V to ~-0.45/+0.57). Checked when numpy
        # is available; the format/prop assertions run regardless.
        try:
            import numpy as np
            u_mean = float(np.asarray(f[1]).mean())
            v_mean = float(np.asarray(f[2]).mean())
            assert abs(u_mean) < 0.3 and abs(v_mean) < 0.3, (u_mean, v_mean)
        except ImportError:
            pass
    assert props.get("AnalogSecamFirstRowComponent") in (b"Db", b"Dr", "Db", "Dr")


def test_secam_440_rows_are_all_decoded(secam_yc):
    """Every 4:4:0 chroma row is a real decoded line, on both planes.

    libchromadec weaves each plane by output-row parity and it spans the whole
    half-height, so misreading first_frame_row as a placement offset shifts a
    plane down a row — blanking its first row, dropping its last, and swapping
    which field its rows belong to. Which plane it hits alternates with the
    ident, so check a few frames.
    """
    np = pytest.importorskip("numpy")
    luma, chroma = secam_yc
    clip = vsanalog.decode_4fsc_video(str(luma), str(chroma), decoder="secam")
    for n in range(min(4, clip.num_frames)):
        with clip.get_frame(n) as f:
            for plane, name in ((1, "Cb"), (2, "Cr")):
                rows = np.asarray(f[plane])
                assert rows.shape[0] == clip.height // 2
                blank = [i for i in range(rows.shape[0]) if not np.abs(rows[i]).any()]
                assert not blank, f"frame {n} {name} rows not decoded: {blank}"


def test_rgb_rejected_for_secam(secam_yc):
    luma, chroma = secam_yc
    with pytest.raises(vs.Error):
        clip = vsanalog.decode_4fsc_video(
            str(luma), str(chroma), decoder="secam", color_family="rgb")
        clip.get_frame(0)


def test_diagnostics_reach_the_core_log(ntsc_tbc):
    """libchromadec's diagnostics arrive as VapourSynth log messages.

    The library emits nothing until a sink is installed, so this also proves
    the plugin installs one. Debug level is the only threshold guaranteed to
    produce output on a clean capture.
    """
    seen: list[tuple[int, str]] = []
    handle = vs.core.add_log_handler(lambda t, m: seen.append((int(t), m)))
    try:
        vsanalog.set_log_level("debug")
        clip = vsanalog.decode_4fsc_video(str(ntsc_tbc))
        with clip.get_frame(0):
            pass
        assert any(t == vs.MESSAGE_TYPE_DEBUG for t, _ in seen), seen

        seen.clear()
        vsanalog.set_log_level("off")
        clip = vsanalog.decode_4fsc_video(str(ntsc_tbc))
        with clip.get_frame(0):
            pass
        assert not seen
    finally:
        vsanalog.set_log_level("info")
        vs.core.remove_log_handler(handle)


def test_set_log_level_rejects_unknown_level():
    with pytest.raises(vs.Error):
        vsanalog.set_log_level("chatty")


def test_returned_failures_are_not_also_logged(ntsc_tbc, tmp_path):
    """A failure reaches the script as an error, not as an error *and* a log line.

    libchromadec announces a failure on the diagnostic channel as well as
    returning it, flagged so a consumer reporting the return path can drop the
    copy. Without that the reason arrives twice.
    """
    sample = tmp_path / "corrupt.tbc"
    sample.write_bytes(ntsc_tbc.read_bytes()[:1 << 16])
    (tmp_path / "corrupt.tbc.json").write_text('{"videoParameters": {"system": "BOGUS"}}')

    seen: list[tuple[int, str]] = []
    handle = vs.core.add_log_handler(lambda t, m: seen.append((int(t), m)))
    try:
        with pytest.raises(vs.Error) as excinfo:
            vsanalog.decode_4fsc_video(str(sample))
        assert "BOGUS" in str(excinfo.value) or "system" in str(excinfo.value)
        assert not [m for _, m in seen if "videoParameters" in m], seen
    finally:
        vs.core.remove_log_handler(handle)
