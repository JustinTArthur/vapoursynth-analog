"""Requesting CUDA on a no-CUDA runner must fall back gracefully, not crash.

Catches regressions in the tbc-tools cold-load probe — the bug that
previously access-violated the process when the CUDA provider DLL was loaded
outside ONNX Runtime's provider bridge. After our patch, a missing or
unloadable CUDA provider falls back to CPU and the decode completes normally.
"""
import vsanalog


def test_cuda_request_falls_back_without_crashing(ntsc_tbc):
    clip = vsanalog.decode_4fsc_video(
        ntsc_tbc,
        decoder="nntransform3d",
        model_version="v2",
        onnx_provider="cuda",
    )
    # The point of this test is process-level survival; the resulting frame
    # will be CPU-decoded on a runner without a CUDA setup.
    with clip.get_frame(0):
        pass