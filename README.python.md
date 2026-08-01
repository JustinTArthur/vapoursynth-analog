# vapoursynth-analog

VapourSynth source and filters plugin for working with digitized analog video
and signals.

[See documentation](https://vapoursynth-analog.justinarthur.com/) for full
details including API reference.

## High-level Python API
If installed as a Python distribution (e.g. with `pip`), the `vsanalog` Python
module wraps the plugin's lower-level API with a type-hinted interface that
loads the plugin automatically.

The primary function provided by the high-level API is
`vsanalog.decode_4fsc_video(…)`, which decodes 4𝑓𝑠𝑐-sampled analog video
signals to digital video clips. It reads time-base-corrected captures from
[ld-decode](https://github.com/happycube/ld-decode) and
[vhs-decode](https://github.com/oyvindln/vhs-decode) (`.tbc`) as well as the
newer CVBS format (`.composite`, or `.y`/`.c`), detected by file extension, and
returns 32-bit float clips: `YUV444PS` (default), `RGBS`, `GRAYS`, or `YUV440PS`
for SECAM. Analytical (NTSC/PAL/SECAM/mono) and neural-network composite
decoders are available.

Example:
```python
import vapoursynth as vs
import vsanalog
import vsdeinterlace  # from vsjetpack

src = vsanalog.decode_4fsc_video(
    './Sources/my_home_video.luma.tbc',
    './Sources/my_home_movie.chroma.tbc',
    decoder='ntsc3d'
)
editable = src.resize.Point(format=vs.YUV444P16)
deinterlaced = vsdeinterlace.QTempGaussMC(editable).deinterlace()
deinterlaced.set_output(0)
```

## Low-level VapourSynth Plugin API
Whether installed as a Python distribution or if the plugin library is dropped
in a VapourSynth plugins directory, the plugin exposes a namespace named 
`analog` available on the `vapoursynth.core` object. Example:
```python
import vapoursynth as vs
src = vs.core.analog.decode_4fsc_video(
  'my_big_production.tbc',
  dropout_correct=True
)
editable = src.resize.Point(format=vs.YUV444P16)
field_match_ref = src.resize.Point(format=vs.YUV444P8)
field_matched = field_match_ref.vivtc.VFM(clip2=editable)
detelecined = field_matched.vivtc.VDecimate()
```

## Installing

The simplest way to install is via pip into a Python environment such as a
venv:
```sh
pip install vsanalog
```
This installs both the native plugin and a Python module with type-hinted
wrappers like `vsanalog.decode_4fsc_video`. The plugin is automatically loaded
when you use the Python module.

The wheel bundles the model weights and whatever shared libraries the plugin
needs, so **VapourSynth** (>= R55) is the only thing you install alongside it.
It runs the neural decoders on the CPU (Linux/Windows) or Apple CoreML (macOS);
GPU-execution-provider wheels are too large for PyPI and are published on
[GitHub Releases](https://github.com/JustinTArthur/vapoursynth-analog/releases)
instead — CUDA/TensorRT and MIGraphX for Linux, CUDA/TensorRT and DirectML for
Windows. The CUDA and MIGraphX wheels require a CUDA 12.x or ROCm 7.x
installation to load at all; the DirectML one does not.

Alternatively, obtain or 
[build](https://vapoursynth-analog.justinarthur.com/en/latest/building.html)
the plugin for your operating system and place vsanalog.dll, vsanalog.dylib,
or vsanalog.so into your VapourSynth plugins directory. The released plugin
binaries need only VapourSynth, but they bundle no models and, off macOS, no
neural-network decoders.

## Implementation Notes
Signal decoding functionality comes from
[libchromadec](https://github.com/JustinTArthur/libchromadec), a Qt-free C-ABI
library extracted from ld-chroma-decoder. It's pulled in as a Meson git wrap
and linked statically, along with the trimmed SQLite it bundles, so this plugin
has no Qt or system-SQLite dependency at all. libchromadec supplies the
composite separation/transformation decoders (NTSC/PAL/SECAM/mono), the CVBS
and TBC readers, dropout masks and corrections, and the neural-network
decoders.

To ease legal distribution, this project is available under the GPL 3 license
(or a compatible one), matching libchromadec.

Machine learning (Claude Opus 4.5 model) was heavily leveraged in the early
development of this plugin to reduce the tedium of gluing the various
components together.

## Alternatives
* jsaowji’s [ldzeug2](https://github.com/jsaowji/ldzeug2) is an excellent
  alternative VapourSynth video source for TBC files that pioneered the neural
  network approaches to separating composited luma and chroma components.
  vapoursynth-analog now exposes those models too (via libchromadec’s
  `ldzeug2_color_cnn` / `ldzeug2_luma_sep` decoders), alongside asdfqazsnbb’s
  nnTransform3D. ldzeug2 moves more 4𝑓𝑠𝑐 processing to the Python domain for
  flexible scripting; it focuses on composite NTSC, ST 170, and Japan format
  signals.
* ld-decode-tools comes with an `ld-chroma-decoder` tool to decode TBC
  files to component R′G′B′ or Y′Cb′Cr′ stream output for use in command line
  workflows and an `ld-dropout-correct` tool for generating a pre-corrected
  intermediate based on upstream dropout detection.
* [tbc-video-export](https://github.com/JuniorIsAJitterbug/tbc-video-export) is
  a convenient wrapper around ld-chroma-decoder and ffmpeg for producing
  digital video files from TBC files. It’s handy if you need to deliver a
  lossless interlaced intermediate to someone else for filtering or color
  grading.
