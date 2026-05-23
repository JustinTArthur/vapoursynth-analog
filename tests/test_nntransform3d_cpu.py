"""NTSC nnTransform3D CPU smoke test - exercises the NN code path, ONNX
Runtime ABI, and bundled-model resolution end-to-end."""
import vsanalog


def test_nntransform3d_cpu_decodes_a_frame(ntsc_tbc):
    clip = vsanalog.decode_4fsc_video(
        ntsc_tbc,
        decoder="nntransform3d",
        model_version="v2",
        onnx_provider="cpu",
    )
    assert clip.num_frames > 0
    with clip.get_frame(0):
        pass
