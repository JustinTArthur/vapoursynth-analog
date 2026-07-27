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
- Added neural-network composite decoders — ``nntransform3d``,
  ``ldzeug2_color_cnn``, ``ldzeug2_luma_sep`` and ``ldzeug2_luma_sep_frame`` —
  with bundled models, ``onnx_provider`` execution-provider selection, and
  graceful CPU fallback. macOS uses native CoreML; Linux/Windows use ONNX
  Runtime.
- Read the newer CVBS capture format (``.composite``, or ``.y``/``.c`` for
  separated luma/chroma) in addition to ld-decode/vhs-decode ``.tbc``, selected
  by file extension. RAW (unscaled-ADC) CVBS encodings are rejected.
- Added ``color_family`` to select ``YUV444PS`` (default), ``RGBS``, or
  ``GRAYS`` float output.
- Added ``chroma_filter``, ``color_difference_precision`` and
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
- ``phase_compensation`` now defaults to enabled, making burst-locked chroma
  demodulation the default.
- An intermediate SQLite metadata sidecar is no longer created for JSON files.
  JSON metadata is processed natively.
- Decoder diagnostics now arrive as VapourSynth log messages instead of going
  straight to the process's stderr, so ``core.add_log_handler`` can capture,
  redirect or silence them. Added ``set_log_level`` to set the threshold
  (``debug``, ``info``, ``warning``, ``critical`` or ``off``). Failures are
  unaffected — they still raise, and now carry the specific complaint from the
  layer that found it rather than a generic summary.

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