"""NTSC ldzeug2 CPU smoke test - exercises the separate ldzeug_decoders
code path (distinct from nnTransform3D's tbc-tools-derived path)."""
import vsanalog


def test_ldzeug2_color_cnn_cpu_decodes_a_frame(ntsc_tbc):
    clip = vsanalog.decode_4fsc_video(
        ntsc_tbc,
        decoder="ldzeug2_color_cnn",
        onnx_provider="cpu",
    )
    assert clip.num_frames > 0
    with clip.get_frame(0):
        pass
