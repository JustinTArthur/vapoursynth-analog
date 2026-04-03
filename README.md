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
[vhs-decode](https://github.com/oyvindln/vhs-decode) then returns `YUV444PS`
(32-bit float) for color decodes, `GRAYS` for monochrome.

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

## Layout

| Directory   | Description                                                                                      |
|-------------|--------------------------------------------------------------------------------------------------|
| `src/`      | C++ VapourSynth plugin source (plugin entrypoint, TBC (incl. chroma) decode, dropout correction) |
| `python/`   | Python package (`vsanalog`) with type-hinted wrapper and PyInstaller hooks                       |
| `extern/`   | Git submodules (`ld-decode-tools`)                                                               |
| `docs/`     | Sphinx documentation source                                                                      |

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
or vsanalog.so into your VapourSynth plugins directory. You'll need the
following runtime dependencies:
- **VapourSynth** (>= R55)
- **Qt6** (Core module)
- **FFTW3**
- **SQLite3**

## Implementation Notes
Signal decoding functionality comes from
[ld-decode-tools](https://github.com/simoninns/ld-decode-tools)’
ld-chroma-decoder. This was done to take advantage of great
work already done on that project including the composite video
separation/transformation decode processes, which would have been hard to
replicate.

Using ld-decode-tools’ code directly (in a submodule here) forces a few design
decisions:
* To ease legal distribution of this plugin, I must make this project available
  under the GPL 3 license or one that’s compatible.
* ld-chroma-decoder’s code relies on QtCore and Qt’s SQLite plugin which would
  make them dependencies. I had a machine learning model write a pure
  SQLite alternative to the JSON to SQLite metadata sidecar converter.
  By avoiding Qt’s SQLite plugin, the plugin is less likely to cause
  crashes from a symbol collision with another linked Qt such as PyQt’s when
  using vspreview.

Machine learning (Claude Opus 4.5 model) was heavily leveraged in the early
development of this plugin to reduce the tedium of gluing the various
components together.

## Alternatives
* jsaowji’s [ldzeug2](https://github.com/jsaowji/ldzeug2) is an excellent
  alternative VapourSynth video source for TBC files and provides neural
  network approaches to separating composited luma and chroma
  components—something that vapoursynth-analog doesn’t have. It moves more
  4𝑓𝑠𝑐 processing to the Python domain for flexible scripting opportunities.
  It focuses on composite NTSC, ST 170, and Japan format signals.
* ld-decode-tools comes with an `ld-chroma-decoder` tool to decode TBC
  files to component R′G′B′ or Y′Cb′Cr′ stream output for use in command line
  workflows and an `ld-dropout-correct` tool for generating a pre-corrected
  intermediate based on upstream dropout detection.
* [tbc-video-export](https://github.com/JuniorIsAJitterbug/tbc-video-export) is
  a convenient wrapper around ld-chroma-decoder and ffmpeg for producing
  digital video files from TBC files. It’s handy if you need to deliver a
  lossless interlaced intermediate to someone else for filtering or color
  grading.
