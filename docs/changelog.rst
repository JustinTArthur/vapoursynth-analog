Changelog
=========
0.3.0 (Not Yet Released)
------------------------
- Replaced the vendored ld-decode-tools submodule with `libchromadec
  <https://github.com/JustinTArthur/libchromadec>`_, a Qt-free C-ABI library
  pulled in as a Meson git wrap and linked statically, along with the trimmed
  SQLite it bundles. Qt is gone entirely, taking the macOS Qt-teardown segfault
  seen in 0.2.x wheels with it, and the released builds link FFTW statically as
  well, so an installed plugin needs nothing but VapourSynth itself.
- Added SECAM decoding, emitting ``YUV440PS`` (4:4:0) with an
  ``AnalogSecamFirstRowComponent`` frame property for downstream chroma
  alignment, tagged ``_ChromaLocation`` top-left. Both chroma planes are woven
  by row parity the way the luma plane is, so separating fields selects the
  same field on all three; an explicit SECAM crop must therefore span a
  multiple of 4 lines.
- Added ``resample_secam`` and ``fill_secam_by_delay`` for turning that 4:4:0
  output into a conventional raster. ``resample_secam`` realigns the
  line-sequential lattice and otherwise behaves like ``core.resize.<Filter>``,
  forwarding ``format``, ``matrix``, ``range`` and the rest;
  ``fill_secam_by_delay`` instead copies each line's missing color difference
  from the previous line of the same field, as a receiver's delay line does,
  interpolating nothing. Both want woven frames — they split the fields
  themselves — so run them before deinterlacing.
- Added ``modernize_chromaticity`` (plugin and Python wrapper), converting
  analog-era colorimetry and photometry to modern targets (BT.709, sRGB,
  BT.2100 PQ/HLG, BT.2020 SDR, DCI/D65 P3, XYZ) with BT.1886 Annex 1/Appendix 1 display modelling, optional Bradford chromatic
  adaptation, and ``resize``-style parameter names whose ``*_in`` values
  override frame properties. Color only: no geometry conversion and no
  dithering. Subsampled Y'CbCr input is converted through an internal
  ``resize`` round trip (``resample_filter_uv``, bicubic by default) and
  keeps its subsampling on Y'CbCr output; 4:4:0 SECAM is instead routed
  through ``resample_secam`` / ``fill_secam_by_delay``. Constant-luminance
  output covers both ``2020cl`` (H.273 matrix 10) and ``chromacl``
  (matrix 13, luma weights derived from the output primaries), each free to
  pair with any output transfer: H.273 Equations E-62 to E-65 define the
  color-difference normalizers as the transfer characteristic function
  applied to expressions in K\ :sub:`B`/K\ :sub:`R`, so the curve is an
  independent axis rather than something the matrix fixes.
- Added ``amplify_chroma`` (plugin and Python wrapper), a post-decode
  counterpart to ``chroma_gain``: a saturation control in the analog video
  domain, scaling E'Cb/E'Cr rather than an HSV/HLS saturation axis. Frames
  that already carry those color differences — 32-bit float Y'CbCr tagged
  ``_Matrix=4``, ``5`` or ``6``, as ``decode_4fsc_video`` emits — are scaled
  in place, and the same color differences in an integer format go through a
  float intermediate that names no matrix, so nothing is resampled and even
  4:4:0 SECAM survives. Frames on other axes are instead converted through a
  ``_Matrix=6`` intermediate of the same subsampling and back, per frame, so
  previously captured video from a source plugin works too; that conversion
  holds the analog luma constant the way a decoder's gain does, leaving the
  frame's own Y' to be re-derived, and 4:4:0 is refused rather than blended.
  Only analog-era ``_Primaries`` (4, 5, 6, 7, or none) are accepted, so it
  belongs upstream of ``modernize_chromaticity``.
- Added neural-network composite decoders — ``nntransform3d``,
  ``ldzeug2_color_cnn``, ``ldzeug2_luma_sep`` and ``ldzeug2_luma_sep_frame`` —
  with bundled models, ``onnx_provider`` execution-provider selection, and
  graceful CPU fallback. macOS runs them on native CoreML — the Neural Engine
  on Apple silicon — and the Windows wheel on any DirectX 12 GPU through
  DirectML; separately published GPU wheels add Nvidia CUDA/TensorRT (Linux,
  Windows) and AMD MIGraphX (Linux). See :doc:`installation`.
- Read the newer CVBS capture format (``.cvbs``, or ``.cvbsy``/``.cvbsc`` for
  separated luma/chroma, plus the pre-1.5.0 ``.composite`` and ``.y``/``.c``
  spellings) in addition to ld-decode/vhs-decode ``.tbc``, selected by file
  extension. RAW (unscaled-ADC) CVBS encodings are rejected.
- Added ``color_family`` to select ``YUV444PS`` (default), ``RGBS``, or
  ``GRAYS`` float output.
- Added ``model_precision``, ``color_difference_precision`` and
  ``broadcast_scaling_precision`` options.
- Output geometry now follows the interface standards instead of the source's
  declared crop, so a video system always decodes to the same raster. NTSC and
  PAL-M give **768x486** (SMPTE ST 244's digital active line, ST 170's active
  picture) and PAL and SECAM give **948x576** (EBU Tech 3280-E and ITU-R
  BT.1700). Previously ``.tbc`` sources inherited their sidecar's crop, giving
  760x488 and 928x576 with the old default padding. The wider horizontal window
  includes the blanking transition on each side of the picture.
- **Removed** ``padding_multiple``. Output is now exactly the active window on
  every source. Under libchromadec the option added a black border rather than
  widening the crop, which ``std.AddBorders`` does downstream with control over
  the amount, sides and color; removing it also means pixel (0, 0) is always
  the first active sample of the first active line.
- **525-line output is now tagged bottom-field-first.** ST 170's 486-line
  window starts on a field 2 half-line, because field 2 begins at line 264 and
  so its first active line (283) sits half a line above field 1's (21). The
  previous top-field-first tag came from the black padding row that the old
  default ``padding_multiple=8`` added above the picture. 625-line output
  begins on a field 1 line and remains top-field-first.
- Added ``first_active_sample``, ``last_active_sample``, ``first_active_line``
  and ``last_active_line`` to crop explicitly, in the numbering of the signal
  standards themselves rather than of the stored raster. Samples are numbered
  as SMPTE ST 244 and EBU Tech 3280-E do, from the start of the digital active
  line, with negative values reaching back into the line blanking ahead of it;
  lines are the field-sequential signal line numbers of SMPTE ST 170, ITU-R
  BT.470 / BT.1700 and EBU Tech 3280, ``first_active_line`` naming the window's
  topmost line. Both bounds are inclusive and each is independent; unset bounds
  keep the standard's value. To restore the previous crop, pass
  ``first_active_sample=9, last_active_sample=768`` for NTSC or ``8`` / ``929``
  for PAL.
- The standard active window is now placed through each source's own horizontal
  alignment instead of a fixed offset, so subcarrier-locked captures — whose
  rows are cut at the first digital blanking sample rather than at 0H, as
  ``ld-chroma-encoder --sc-locked`` output is — land on the same picture as a
  0H-cut ``.tbc`` without an explicit crop. This also fixes PAL-M, whose digital
  active line sits one sample further left than NTSC's.
- Added ``AnalogFirstActiveSample`` / ``AnalogLastActiveSample`` /
  ``AnalogFirstActiveLine`` / ``AnalogLastActiveLine`` frame properties
  reporting the resolved window in those same standards coordinates.
- Added ``annotate_dropouts``, reporting where each frame's dropouts are
  instead of concealing them, in an ``AnalogDropoutSpans`` frame property —
  a flat array of ``y``, ``x_start``, ``x_end``, ``origin`` per region, in the
  decoded clip's own pixel coordinates. It is independent of
  ``dropout_correct``, so damaged regions can be handed to another filter
  rather than replaced from neighbouring lines, and ``dropout_overcorrect``
  widens what gets reported just as it widens what correction overwrites. On
  SECAM the regions also cover the FM click concealment the decoder performed
  itself, distinguished by ``origin``.
- Added ``core.analog.create_dropouts_mask`` (``vsanalog.create_dropouts_mask``)
  to rasterise those regions into a mask clip for ``core.std.MaskedMerge`` and
  the other ``std`` mask functions, with ``origins`` to select which kinds of
  region to draw. The mask matches the clip's dimensions and precision, so
  subsampled output needs no special handling. ``vsanalog.dropout_spans``
  reads the regions back as a list of named tuples.
- A decode asking for dropout correction or annotation now warns when the
  capture's ``.tbc.db`` sidecar carries no dropout metadata while its
  ``.tbc.json`` does. The SQLite sidecar wins on existence alone, and releases
  up to 0.2.3 wrote one themselves during a decode without copying the dropouts
  across, so a capture decoded by an older version reports itself clean however
  damaged it is. Nothing writes those sidecars now; delete the ``.tbc.db`` to
  decode from the JSON.
- ``phase_compensation`` now defaults to enabled, making burst-locked chroma
  demodulation the default.
- Removed ``fpsnum`` and ``fpsden``. They were meant to convert to a constant
  frame rate the way ``bs.VideoSource`` does, but only ever retagged the rate
  and rescaled the frame count — no frame was dropped or duplicated, so raising
  the rate advertised frames past the end of the capture and lowering it put the
  tail out of reach. A 4𝑓𝑠𝑐 capture is constant-rate by construction, leaving
  nothing to convert; retag with ``core.std.AssumeFPS``, and retime after
  deinterlacing, where dropping a frame doesn't mean dropping two fields.
- An intermediate SQLite metadata sidecar is no longer created for JSON files.
  JSON metadata is processed natively.
- Decoder diagnostics now arrive as VapourSynth log messages instead of going
  straight to the process's stderr, so ``core.add_log_handler`` can capture,
  redirect or silence them. Added ``set_log_level`` to set the threshold
  (``debug``, ``info``, ``warning``, ``critical`` or ``off``). Failures are
  unaffected — they still raise, and now carry the specific complaint from the
  layer that found it rather than a generic summary.
- FFTW is now built from source as a Meson subproject rather than taken from the
  platform package, because the packaged builds ship the wrong codelets on
  several targets — most importantly, both Homebrew and Fedora produce a
  scalar-only aarch64 FFTW. The gain is concentrated in the 3D decoders, which
  spend most of their time in FFTW, and tracks SIMD vector width: ``transform3d``
  decodes about 17% faster on both arm64 targets (scalar to 128-bit NEON) and
  about 1% faster on x86_64 Linux (256-bit AVX to 256-bit AVX2, no width change).
  ``transform2d`` moves only a few percent anywhere. Output is numerically
  unchanged. On x86_64 the wrap is kept for uniformity and for the build fix
  below rather than for speed. FFTW is also no longer a build dependency, which
  fixes installing from an sdist on a machine that has no FFTW: that used to fail
  the compile outright,
  because libchromadec includes ``<fftw3.h>`` unconditionally despite declaring
  the dependency optional. Pass ``-Dforce_fallback_for=`` to link a system copy
  instead.
- Fixed the Windows wheel's plugin failing VapourSynth's autoload, which left
  ``core.analog`` unavailable to scripts that had not imported ``vsanalog``
  first. Its bundled dependency DLLs were reachable only through a search path
  that the ``vsanalog`` import registers and the autoloader never runs.

0.2.3
-----
- Fix fractional IRE value input for blank/black/white in JSON to SQLite
  conversion.
- If chroma metadata sidecar is missing, fallback to luma/main metadata.

0.2.2
-----
- Y/C-separated signals correctly force ``mono`` decoder for luma processing.
- Y/C-separated decodes scale chroma planes against the chroma 4fsc's own
  IRE excursion instead of luma signal’s, fixing potential mismatch.

0.2.1
-----
- Windows wheel now contains full dependency set.

0.2.0
------------------
- Added ``vsanalog`` Python wrapper package with type-hinted signatures and
  auto-loading fallback for older VapourSynth versions that predate
  pip-installable plugins.
- Retagged wheels as independent from the CPython ABI so a single wheel can
  serve any compatible Python interpreter on a given platform.
- More comprehensive documentation.

0.1.1
-----
New build pipeline with reasonably-packaged plugin libraries plus Python
sdists and wheels that can support VapourSynth's upcoming pip-installable
plugin flow. The old drop-in plugin libraries continue to work.

No fixes or feature changes to the plugin itself.

0.1.0
-----
First release of the plugin. The sole ``decode_4fsc_video`` VapourSynth
function is mostly derived from ``ld-decode-tools``, incorporating its
composite video separation/transformation decode processes along with its
dropout correction methods.