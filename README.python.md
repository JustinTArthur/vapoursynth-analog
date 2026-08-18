# vapoursynth-analog

VapourSynth source and filters plugin for working with digitized analog video
and signals.

[See documentation](https://vapoursynth-analog.justinarthur.com/) for full
details including API reference.

## What it does
* **Decodes 4𝑓𝑠𝑐-sampled captures** to 32-bit float clips: time-base-corrected
  `.tbc` files from [ld-decode](https://github.com/happycube/ld-decode) and
  [vhs-decode](https://github.com/oyvindln/vhs-decode), and the newer CVBS
  format (`.cvbs`, or `.cvbsy`/`.cvbsc` for separated luma/chroma). NTSC
  (Comb), PAL (Transform/PalColour), SECAM, and neural-network composite
  [decoders][api-decoders] are available, the last running on CPU, GPU, or
  Apple Neural Engine.
* **Handles dropouts** three ways: [correction][api-dropout-correction] during
  decode, from the capture itself or from additional captures of the same
  content; [annotation][api-dropout-annotation] as frame properties that
  survive trimming and splicing; and a [mask clip][api-dropouts] for your own
  repair filters.
* **Modernizes colorimetry**: analog-era primaries and transfer
  characteristics to BT.709, sRGB, BT.2020 SDR or BT.2100 PQ/HLG in one
  [color-managed step][api-colorimetry], on any clip, whether a fresh decode
  or a conventional capture.
* **Adjusts chroma gain** in analog terms: a gain on the color-difference
  signals, not the saturation axis of an HSV model.
* **Turns SECAM's 4:4:0 lattice** into a conventional raster, by
  [resampling or delay-line fill][api-secam].

[api-decoders]: https://vapoursynth-analog.justinarthur.com/en/latest/python-api.html#decoder-options
[api-dropout-correction]: https://vapoursynth-analog.justinarthur.com/en/latest/python-api.html#dropout-correction
[api-dropout-annotation]: https://vapoursynth-analog.justinarthur.com/en/latest/python-api.html#dropout-annotation
[api-dropouts]: https://vapoursynth-analog.justinarthur.com/en/latest/python-api.html#dropouts
[api-colorimetry]: https://vapoursynth-analog.justinarthur.com/en/latest/python-api.html#colorimetry
[api-secam]: https://vapoursynth-analog.justinarthur.com/en/latest/python-api.html#secam-chroma
[recipes]: https://vapoursynth-analog.justinarthur.com/en/latest/recipes.html

## High-level Python API
If installed as a Python distribution (e.g. with `pip` or `uv`), the `vsanalog`
Python module wraps the plugin's lower-level API with a type-hinted interface
that loads the plugin automatically. `vsanalog.decode_4fsc_video(…)` is the
starting point, returning 32-bit float clips: `YUV444PS` (default), `RGBS`
(`color_family="rgb"`), `GRAYS` (`color_family="gray"`), or `YUV440PS` for
SECAM.

Example:
```python
import vapoursynth as vs
import vsanalog
import vsdeinterlace  # from vsjetpack

src = vsanalog.decode_4fsc_video(
    './Sources/my_home_video.tbc',
    './Sources/my_home_movie_chroma.tbc',
    decoder='ntsc3d'
)
deinterlaced = vsdeinterlace.QTempGaussMC().deinterlace(src)
modern = vsanalog.modernize_chromaticity(
  deinterlaced,
  primaries_in_s='ntsc-1953',
  output_preset='hdtv'
)
universal = modern.resize.Spline36(
  format=vs.YUV420P8,
  chromaloc=vs.CHROMA_LEFT,
  dither_type='random'
)
universal.set_output(0)
```
The source decode and color conversion above use vsanalog; the rest are
ordinary VapourSynth or 3rd-party modules. The [Recipes][recipes] page has
complete scripts for cropping out caption data, inverting telecine, CRT-style
interlaced display, deinterlacing, and denoising.

## Low-level VapourSynth Plugin API
Whether installed as a Python distribution or if the plugin library is dropped
in a VapourSynth plugins directory, the plugin exposes a namespace named
`analog` available on the `vapoursynth.core` object. It carries the natively
implemented functions `decode_4fsc_video`, `create_dropouts_mask`,
`modernize_chromaticity`, `amplify_chroma`, and `set_log_level`. Low-level API
example:
```python
from vapoursynth import core
src = core.analog.decode_4fsc_video(
  'my_big_production.tbc',
  dropout_correct=True
)
```

SECAM helpers and `dropout_spans` are Python-side conveniences available only
through the higher-level `vsanalog` module.

## Installing

The simplest way to install is via pip into a Python environment such as a
venv:
```sh
pip install vsanalog
```
This installs both the native plugin and a Python module with type-hinted
wrappers like `vsanalog.decode_4fsc_video`. The plugin is automatically loaded
when you use the Python module.

Alternatively, obtain or 
[build](https://vapoursynth-analog.justinarthur.com/en/latest/building.html)
the plugin for your operating system and place vsanalog.dll, vsanalog.dylib,
or vsanalog.so into your VapourSynth plugins directory. The released plugin
binaries need only **VapourSynth** (>= R55): libchromadec, its SQLite, and FFTW
are linked into the plugin. The standalone plugin differs from the wheel in two
ways: neural-network decoders are only available on macOS (thanks to native
CoreML), and no model weights are bundled, so neural decoding through them
takes a `model_path` you supply.

### GPU-accelerated neural decoding

The PyPI wheels run neural decoders on CPU (Linux), on any DX12 GPU via
DirectML with CPU fallback (Windows), or on Apple CoreML (macOS). The
remaining GPU-execution-provider wheels are too large for PyPI and are
published on the project's own package index, one channel per vendor runtime:
TensorRT/CUDA (Nvidia) for Linux and Windows, MIGraphX (AMD) for
Linux. Add the channel for yours as an extra index:
```sh
pip install vsanalog --extra-index-url https://py.justinarthur.com/cu13/   # CUDA 13
pip install vsanalog --extra-index-url https://py.justinarthur.com/cu12/   # CUDA 12 (pre-Turing cards)
pip install vsanalog --extra-index-url https://py.justinarthur.com/rocm7/  # MIGraphX on ROCm 7
```
`vsanalog[tensorrt]` on a CUDA channel pulls in the matching TensorRT and
CUDA libraries from PyPI as a convenience (`vsanalog[cuda]` for the CUDA
runtime alone). These would be needed if you didn't already have them
installed environment-wide. See the
[installation guide](https://vapoursynth-analog.justinarthur.com/en/latest/installation.html) for details.

## Implementation Notes
Signal decoding functionality comes from
[libchromadec](https://github.com/JustinTArthur/libchromadec), a C-ABI library
started from the original ld-chroma-decoder. It's pulled in as a Meson git wrap
and linked statically, along with the trimmed SQLite it bundles, so this plugin
has no Qt or system-SQLite dependency at all. libchromadec supplies the composite
separation/transformation decoders (NTSC/PAL/SECAM/mono), the CVBS and TBC
readers, dropout masks and corrections, and the neural-network decoders.

To ease legal distribution, this project is available under the GPL 3 or later
license, matching libchromadec and its inherited ld-decode history.

Machine learning models have been employed at times in the development of this
plugin.

## Alternatives
* jsaowji’s [ldzeug2](https://github.com/jsaowji/ldzeug2) is an excellent
  alternative VapourSynth video source for TBC files that pioneered the neural
  network approaches to separating composited luma and chroma components.
  vapoursynth-analog now exposes those models too (via libchromadec's
  `ldzeug2_color_cnn` / `ldzeug2_luma_sep` decoders). ldzeug2 moves more 4𝑓𝑠𝑐
  processing to the Python domain for flexible scripting; it focuses on
  composite NTSC, ST 170, and Japan format signals.
* ifb's [vapoursynth-composite](https://github.com/ifb/vapoursynth-composite)
  can take decodes, conventional captures, and BT.601-sampled sources like DV
  and DVD back into digitized composite form for analog-domain repairs and
  improvements.
* [tbc-tools](https://github.com/harrypm/tbc-tools) comes with an
  `ld-chroma-decoder` tool to decode TBC files to component R′G′B′ or
  Y′ C′b C′r stream output for use in command line workflows and an
  `ld-dropout-correct` tool for generating a pre-corrected intermediate based
  on upstream dropout detection. Split from the original ld-decode project's
  tools dir.
* [tbc-video-export](https://github.com/JuniorIsAJitterbug/tbc-video-export) is
  a convenient wrapper around ld-chroma-decoder and ffmpeg for producing
  digital video files from TBC files. It’s handy if you need to deliver a
  lossless interlaced intermediate to someone else for filtering or color
  grading.
* [decode-orc](https://github.com/decode-orc/decode-orc) helps orchestrate
  processing and filtration of TBC/CVBS signal data, providing a filter graph
  like VapourSynth's but in a closer-to-analog space. Also provides a plugin
  API and an R′G′B′ or Y′ C′b C′r output sink with similar code heritage
  as libchromadec's.
