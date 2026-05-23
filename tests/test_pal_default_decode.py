"""PAL default-decoder smoke test - no neural network, exercises the
plugin function, tbcreader, sqlite metadata, and the comb-decoder paths."""
import vapoursynth as vs


def test_pal_default_decoder_produces_a_frame(pal_tbc):
    clip = vs.core.analog.decode_4fsc_video(pal_tbc)
    assert clip.num_frames > 0
    assert clip.width > 0 and clip.height > 0
    with clip.get_frame(0) as f:
        assert f is not None