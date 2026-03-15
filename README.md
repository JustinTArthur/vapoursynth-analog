# vapoursynth-analog

VapourSynth source and filters plugin for working with digitized analog video
and signals.

## VapourSynth Functions

### `decode_4fsc_video`
Decodes 4𝑓𝑠𝑐-sampled analog video signals to a digital video clip. The signal
data must be orthogonal video system lines with well-formed blanking and
syncing structure at a stable time base such as those from
[ld-decode](https://github.com/happycube/ld-decode) and 
[vhs-decode](https://github.com/oyvindln/vhs-decode). These files normally have
a .tbc extension indicating they are time-base-corrected and must have a
metadata sidecar file in JSON or SQlite format.

For color decodes, this returns a clip (VideoNode) in VapourSynth’s `YUV444PS`
format. For monochrome decodes, the clip is in VapourSynth’s `GRAYS` format.
Behind the scenes, it employs
[ld-decode](https://github.com/happycube/ld-decode)’s ld-chroma-decoder
routines.

```python
import vapoursynth as vs
from vapoursynth import core

clip = core.analog.decode_4fsc_video("/path/to/capture.tbc")

# The resulting YUV444PS format retains maximum quality from the decode, but
# the format is uncommon. Filters may not expect or work well with it. You’ll
# often want to convert it to something else using the built-in resize plugin
# or fmtconv.
# R′G′B′ for color/levels adjustment filters:
workable_clip = clip.resize.Point(format=vs.RGBS)
# For mvtools2 pipelines like QTGMC that require an integer format:
workable_clip = clip.resize.Point(format=vs.YUV444P16)
# Reducing chroma resolution closer to analog source aids some NR filters:
workable_clip = clip.resize.Spline36(format=vs.YUV422P16)
```

Y/C-separated signals such as from vhs-decode or S-Video capture are supported:
```python
clip = core.analog.decode_4fsc_video("luma.tbc", "chroma.tbc")
```

#### Parameters

| Parameter                                 | Type | Default | Description |
|-------------------------------------------|------|---------|-------------|
| `composite_or_luma_source`                | string | (required) | Path to the composite or luma .tbc file |
| `chroma_or_pb_source`                     | string | (none) | Path to separate chroma .tbc file (for Y/C separated sources) |
| `pr_source`                               | string | (none) | Path to Pr component .tbc file (component video, not yet supported) |
| `decoder`                                 | string | auto | Decoder to use (see below) |
| `chroma_gain`                             | float | 1.0 | Chroma gain multiplier |
| `chroma_phase`                            | float | 0.0 | Chroma phase adjustment in degrees |
| `chroma_nr`                               | float | 0.0 | Chroma noise reduction level (NTSC only) |
| `luma_nr`                                 | float | 0.0 | Luma noise reduction level |
| `phase_compensation`                      | int | 0 | NTSC phase compensation (set to 1 to enable) |
| `padding_multiple`                        | int | 8 | Round output dimensions to multiple of this value (0 to disable) |
| `dropout_correct`                         | int | 0 | Enable dropout correction using metadata-identified dropouts (1 = on) |
| `dropout_overcorrect`                     | int | 0 | Extend dropout boundaries by ±24 samples to catch sloped edges on heavily damaged sources |
| `dropout_intra`                           | int | 0 | Force intra-field only correction; avoids inter-field borrowing artifacts on high-motion content |
| `dropout_composite_or_luma_extra_sources` | data[] | | Additional TBC files for multi-source dropout correction |
| `dropout_chroma_extra_sources`            | data[] | | Additional chroma TBC files for multi-source dropout correction (for color-under formats) |
| `reverse_fields`                          | int | 0 | Set to 1 to swap field order |
| `fpsnum`                                  | int | auto | Override frame rate numerator |
| `fpsden`                                  | int | 1 | Override frame rate denominator |

Each source signal file must have a corresponding metadata sidecar file with
the same base name and a .db or .json extension. If the metadata is in JSON
format, a .db file will automatically be created in the same directory.

#### Decoder Options

The `decoder` parameter accepts the same values as ld-chroma-decoder:

| Decoder | Video System | Description |
|---------|--------------|-------------|
| `ntsc1d` | NTSC | 1D comb filter |
| `ntsc2d` | NTSC | 2D comb filter (default for NTSC) |
| `ntsc3d` | NTSC | 3D adaptive comb filter |
| `ntsc3dnoadapt` | NTSC | 3D comb filter without adaptation |
| `pal2d` | PAL | 2D PALcolour filter (default for PAL) |
| `transform2d` | PAL | 2D Transform PAL frequency-domain filter |
| `transform3d` | PAL | 3D Transform PAL frequency-domain filter |
| `mono` | Any | Luma-only decode (no chroma) |

If not specified, the decoder is auto-selected based on the video system in the
TBC metadata.

#### Dropout Correction

Setting `dropout_correct=1` replaces signal dropout regions identified in the
TBC metadata with data from nearby clean lines, based on the algorithm from
[ld-decode-tools](https://github.com/simoninns/ld-decode-tools)’
ld-dropout-correct. Luma and chroma are sourced independently using FIR 
frequency separation to find the closest match for each.

For multi-source correction, pass additional TBC captures of the same content
via `dropout_composite_or_luma_extra_sources` (and `dropout_chroma_extra_sources`
for Y/C-separated formats). Sources are aligned using VBI frame numbers when
available (laserdisc CAV/CLV), falling back to sequential frame alignment for
sources without VBI data (e.g., VHS-decode output).

When dropout correction is enabled, the following frame properties are set:

| Property                       | Type | Description                                |
|--------------------------------|------|--------------------------------------------|
| `AnalogDropoutsCorrected`      | int  | Dropout regions successfully replaced      |
| `AnalogDropoutsFailed`         | int  | Dropout regions where no replacement found |
| `AnalogDropoutsTotalDistance`   | int  | Sum of line distances for all replacements |

## Runtime Dependencies

- **VapourSynth** (>= R55)
- **Qt6** (Core module)
- **FFTW3**
- **SQLite3**

### Installing Runtime Dependencies

**macOS (Homebrew):**
```bash
brew install qt6 fftw sqlite
```

**Ubuntu/Debian:**
```bash
sudo apt install libqt6core6 libfftw3-3 libsqlite3-0
```

**Fedora:**
```bash
sudo dnf install qt6-qtbase fftw-libs sqlite-libs
```

**Arch Linux:**
```bash
sudo pacman -S qt6-base fftw sqlite
```

**Windows:**

Install VapourSynth, then ensure Qt6, FFTW3, and SQLite3 DLLs are available in
your PATH or alongside the plugin.

### Installing The Plugin

The simplest way to install is via pip into a Python environment such as a 
venv:
```sh
pip install vsanalog
```
This installs both the native plugin and a Python module with type-hinted
wrappers like `vsanalog.decode_4fsc_video`. The plugin is automatically loaded
when you use the Python module.

Alternatively, obtain or build the plugin for your operating system and place
vsanalog.dll, vsanalog.dylib, or vsanalog.so into your VapourSynth plugins
directory.

## Building from Source

See [BUILDING.md](BUILDING.md) for build dependencies and instructions.

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
  4fsc processing to the Python domain for flexible scripting opportunities.
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
