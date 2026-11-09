Python API
==========
The ``vsanalog`` Python package provides a high-level, type-hinted interface to
the vsanalog VapourSynth plugin. It handles plugin loading automatically and
accepts Python-native types like :py:class:`~pathlib.Path` and :py:class:`bool`.

Decoding
--------
.. py:function:: vsanalog.decode_4fsc_video(\
        composite_or_luma_source, \
        chroma_or_pb_source=None, \
        pr_source=None, \
        *, \
        decoder=None, \
        color_family=None, \
        color_difference_precision=None, \
        broadcast_scaling_precision=None, \
        model_version=None, \
        model_path=None, \
        model_input_scale=None, \
        model_precision=None, \
        onnx_provider=None, \
        model_chroma_bandpass=None, \
        reverse_fields=False, \
        chroma_gain=1.0, \
        chroma_phase=0.0, \
        chroma_nr=0.0, \
        luma_nr=0.0, \
        phase_compensation=True, \
        first_active_sample=None, \
        last_active_sample=None, \
        first_active_line=None, \
        last_active_line=None, \
        dropout_correct=False, \
        dropout_overcorrect=False, \
        dropout_intra=False, \
        annotate_dropouts=False, \
        dropout_composite_or_luma_extra_sources=None, \
        dropout_chroma_extra_sources=None)

    Decode 4𝑓𝑠𝑐 (four times subcarrier frequency) sampled analog video
    signals to a digital video clip. The signal data must be orthogonal video
    system lines with well-formed blanking and syncing structure at a stable
    time base, such as those produced by
    `ld-decode <https://github.com/happycube/ld-decode>`_ and
    `vhs-decode <https://github.com/oyvindln/vhs-decode>`_. These files
    normally have a ``.tbc`` extension indicating they are time-base-corrected
    and must have a metadata sidecar file in JSON or SQLite format. The newer
    CVBS format is also read, detected by extension: ``.cvbs`` for composite,
    or ``.cvbsy``/``.cvbsc`` for separated luma/chroma (with a ``.meta``
    sidecar); the pre-1.5.0 ``.composite`` and ``.y``/``.c`` spellings are
    still accepted. RAW (unscaled-ADC) CVBS encodings are not supported.

    Returns a 32-bit float clip whose format depends on ``color_family``:
    ``YUV444PS`` (default), ``RGBS``, or ``GRAYS``. SECAM decodes to
    ``YUV440PS`` (4:4:0).

    :param composite_or_luma_source:
        Path to the composite or luma-only ``.tbc`` file.
    :type composite_or_luma_source: :py:class:`str` | :py:class:`~pathlib.Path`

    :param chroma_or_pb_source:
        Path to a separate chroma ``.tbc`` file, for Y/C-separated sources
        such as S-Video or VHS color-under.
    :type chroma_or_pb_source: :py:class:`str` | :py:class:`~pathlib.Path` | None

    :param pr_source:
        Path to the Pr component ``.tbc`` file. Y/Pb/Pr component video is not
        yet supported; passing this raises an error.
    :type pr_source: :py:class:`str` | :py:class:`~pathlib.Path` | None

    :param decoder:
        Chroma decoder to use. Analytical: ``"ntsc1d"``, ``"ntsc2d"``,
        ``"ntsc3d"``, ``"ntsc3dnoadapt"``, ``"pal2d"``, ``"transform2d"``,
        ``"transform3d"``, ``"hvd2d"``, ``"hvd3d"``, ``"secam"``, or
        ``"mono"``. Neural-network
        (NTSC only): ``"nntransform3d"``, ``"ldzeug2_color_cnn"``,
        ``"ldzeug2_luma_sep"``, ``"ldzeug2_luma_sep_frame"``. When ``None``, the
        decoder is chosen automatically based on the video system. See
        :ref:`decoder-options`. When ``chroma_or_pb_source`` is supplied, this
        decoder applies to the chroma TBC only; the luma TBC is read with the
        ``"mono"`` decoder so it isn't run through Y/C separation a second
        time.
    :type decoder: :py:class:`str` | None

    :param color_family:
        Output family: ``"yuv"`` (default; ``YUV444PS``, or ``YUV440PS`` for
        SECAM), ``"rgb"`` (``RGBS``; not available for SECAM), or ``"gray"``
        (``GRAYS`` luma only).
    :type color_family: :py:class:`str` | None

    :param color_difference_precision:
        Color-difference matrix precision: ``"classic"`` or ``"modern"``.
    :type color_difference_precision: :py:class:`str` | None

    :param broadcast_scaling_precision:
        Broadcast-safe scaling precision: ``"classic"``, ``"modern"`` or
        ``"scientific"``.
    :type broadcast_scaling_precision: :py:class:`str` | None

    :param model_version:
        Bundled model to use for a neural-network ``decoder`` (defaults per
        decoder). Ignored when ``model_path`` is given. Choices:
        ``"nntransform3d"``: ``"v1_202512"``, ``"v1_202603"`` (the ``v1``
        series, predating ``v2``'s larger input-magnitude scale), or
        ``"v2"`` (default); ``"ldzeug2_color_cnn"``: ``"1031640"``,
        ``"denoise_613928_ft22k"``, or ``"v2_alot"`` (default);
        ``"ldzeug2_luma_sep"``: ``"2dgray_fields"`` (default, only choice);
        ``"ldzeug2_luma_sep_frame"``: ``"2d_frame_gray_gray_run2_latest"``
        (default, only choice).

        On Apple silicon the bundled ``nntransform3d`` ``"v2"`` package is
        converted at fp16, which is what makes it eligible for the Apple
        Neural Engine — the fastest placement measured, and the reason
        ``"v2"``'s weights scale their input magnitudes down. Its masks differ
        from the fp32 reference well below tape noise. Every other bundled
        package, on every platform, is fp32.
    :type model_version: :py:class:`str` | None

    :param model_path:
        Path to custom model weights (``.onnx``, or a CoreML ``.mlpackage`` on
        macOS) for a neural-network ``decoder``.
    :type model_path: :py:class:`str` | :py:class:`~pathlib.Path` | None

    :param model_input_scale:
        Override the model input magnitude divisor (``nntransform3d`` only).
    :type model_input_scale: :py:class:`float` | None

    :param model_precision:
        Compute precision a neural-network ``decoder``'s backend may use:
        ``"fp32"`` or ``"fp16"``. Permission rather than a guarantee. TensorRT
        acts on it by building a mixed fp16/fp32 engine from the bundled fp32
        weights (fp16 and fp32 engines are cached separately, so changing this
        never reuses an engine built the other way). CUDA and DirectML have no
        such engine mode, so on those the wheel instead bundles a pre-converted
        fp16 copy of the weights, used only when ``onnx_provider`` is pinned to
        ``"cuda"`` or ``"directml"`` — an ``"auto"`` request that lands on the
        CUDA provider stays fp32. Every other backend ignores the setting.
        Defaults to ``"fp16"`` for the bundled ``nntransform3d`` ``"v2"``
        weights and ``"fp32"`` for everything else, including any custom
        ``model_path`` (nothing here can inspect an arbitrary model's training
        scale, and fp16 on weights that overflow it yields NaN output).
        Unrelated to the macOS packages' precision, which is fixed when they
        are converted.
    :type model_precision: :py:class:`str` | None

    :param onnx_provider:
        Execution provider for a neural-network ``decoder``: ``"auto"``,
        ``"cpu"``, ``"tensorrt"``/``"trt"``, ``"cuda"``/``"gpu"``,
        ``"migraphx"``, ``"directml"``, or ``"coreml"``. ``"auto"`` picks the
        accelerator the wheel carries — TensorRT ahead of CUDA on the Nvidia
        wheels — and an unavailable accelerator falls back to CPU.
    :type onnx_provider: :py:class:`str` | None

    :param model_chroma_bandpass:
        Toggle the post-demodulation I/Q low-pass (``ldzeug2_luma_sep`` and
        ``ldzeug2_luma_sep_frame`` only).
    :type model_chroma_bandpass: :py:class:`bool` | None

    :param bool reverse_fields:
        Swap field order.

    :param float chroma_gain:
        Chroma gain multiplier for saturation adjustment.

    :param float chroma_phase:
        Chroma phase adjustment in degrees.

    :param float chroma_nr:
        Chroma noise-reduction level. Only applies to NTSC decoders.

    :param float luma_nr:
        Luma noise-reduction level.

    :param bool phase_compensation:
        Burst-locked NTSC chroma demodulation, recovering the subcarrier phase
        from each line's colorburst instead of assuming it's locked to the
        4𝑓𝑠𝑐 sample grid. Set to ``False`` to force fixed-phase demodulation.
        The PAL decoders are burst-locked by design and ignore this.

    :param int first_active_sample:
    :param int last_active_sample:
        Inclusive horizontal crop, in the sample numbering of the 4𝑓𝑠𝑐 interface
        standards (SMPTE ST 244, EBU Tech 3280-E): sample 0 is the first sample
        of the digital active line, and negative numbers reach back into the
        line blanking ahead of it. Defaults to the whole digital active line.
        See :ref:`active-window`.

    :param int first_active_line:
    :param int last_active_line:
        Inclusive vertical crop, in the standards' field-sequential signal line
        numbers. ``first_active_line`` is the window's topmost line. Defaults to
        the video system's standard active picture.

    :param bool dropout_correct:
        Enable dropout correction using metadata-identified dropouts. See
        :ref:`dropout-correction`.

    :param bool dropout_overcorrect:
        Extend dropout boundaries by +/-24 samples (for heavily damaged
        sources).

    :param bool dropout_intra:
        Force intra-field-only dropout correction, avoiding inter-field
        borrowing artifacts on high-motion content.

    :param bool annotate_dropouts:
        Record each frame's dropout regions in the ``AnalogDropoutSpans`` frame
        property, for :py:func:`vsanalog.dropout_spans` and
        :py:func:`vsanalog.create_dropouts_mask`. Independent of
        ``dropout_correct``; ``dropout_overcorrect`` widens what gets reported.
        See :ref:`dropout-annotation`.

    :param dropout_composite_or_luma_extra_sources:
        Additional composite or luma ``.tbc`` files for multi-source dropout
        correction.
    :type dropout_composite_or_luma_extra_sources: :py:class:`~collections.abc.Sequence`\[:py:class:`str` | :py:class:`~pathlib.Path`] | None

    :param dropout_chroma_extra_sources:
        Chroma ``.tbc`` files for multi-source dropout correction of a separate
        chroma source. Paired by position with
        ``dropout_composite_or_luma_extra_sources``, so each entry is the chroma
        file of the capture at that position and the lists must be the same
        length. Leave unset to correct only luma from the extra captures.
    :type dropout_chroma_extra_sources: :py:class:`~collections.abc.Sequence`\[:py:class:`str` | :py:class:`~pathlib.Path`] | None

    :rtype: :py:class:`VideoNode`

.. _decoder-options:

Decoder Options
~~~~~~~~~~~~~~~
The ``decoder`` parameter accepts the following values:

.. list-table::
    :header-rows: 1
    :widths: 20 15 65

    * - Decoder
      - Video System
      - Description
    * - ``"ntsc1d"``
      - NTSC
      - 1D comb filter
    * - ``"ntsc2d"``
      - NTSC
      - 2D comb filter (default for NTSC)
    * - ``"ntsc3d"``
      - NTSC
      - 3D adaptive comb filter
    * - ``"ntsc3dnoadapt"``
      - NTSC
      - 3D comb filter without adaptation
    * - ``"pal2d"``
      - PAL
      - 2D PALcolour filter (default for PAL)
    * - ``"transform2d"``
      - PAL
      - 2D Transform PAL frequency-domain filter
    * - ``"transform3d"``
      - PAL
      - 3D Transform PAL frequency-domain filter
    * - ``"hvd2d"``
      - NTSC, PAL
      - Holographic-variational Y/C separation (hvd-core), woven-frame path
    * - ``"hvd3d"``
      - NTSC, PAL
      - Holographic-variational Y/C separation through the temporal
        field-sequence pipeline; much slower than ``"hvd2d"``
    * - ``"secam"``
      - SECAM
      - Line-sequential FM chroma; outputs ``YUV440PS`` (4:4:0), see
        :ref:`secam-chroma`
    * - ``"nntransform3d"``
      - NTSC
      - Neural 3D transform Y/C separation
    * - ``"ldzeug2_color_cnn"``
      - NTSC
      - Neural chroma separation/color CNN
    * - ``"ldzeug2_luma_sep"``
      - NTSC
      - Neural luma separation, field mode
    * - ``"ldzeug2_luma_sep_frame"``
      - NTSC
      - Neural luma separation, frame mode
    * - ``"mono"``
      - Any
      - Luma-only decode (no chroma)

If not specified, the decoder is auto-selected based on the video system. The
neural-network decoders are NTSC-only and run a model through ONNX Runtime (or
CoreML on macOS): the wheels bundle weights for each, selected by
``model_version``, or supply your own with ``model_path``. Which accelerator runs
them is chosen by ``onnx_provider`` — see :doc:`installation` for the
execution providers each wheel carries.


.. _active-window:

Active Window
~~~~~~~~~~~~~
Output geometry is pinned to the interface standard for the detected video
system, rather than inherited from whatever crop the source declares, so a
given system always decodes to the same raster:

.. list-table::
    :header-rows: 1
    :widths: 18 22 22 20 18

    * - Video System
      - Samples
      - Lines
      - Output
      - Field Order
    * - NTSC / PAL-M
      - 0..767 (768)
      - 283..263 (486)
      - 768x486
      - Bottom first
    * - PAL / SECAM
      - 0..947 (948)
      - 23..623 (576)
      - 948x576
      - Top first

Both axes are given in the numbering of the standards themselves, and both
bounds are inclusive. Samples are numbered as SMPTE ST 244 (525-line) and EBU
Tech 3280-E (625-line) do, from the start of the digital active line. Lines are
the field-sequential signal line numbers of SMPTE ST 170, ITU-R BT.470 /
BT.1700 and EBU Tech 3280.

The horizontal window is the **digital active line**, which is deliberately
wider than the analog picture and so carries the blanking transition on each
side (PAL 948 samples versus 922 of picture; NTSC 768 versus 754). The vertical
window is the standard active picture: SMPTE ST 170's 486 lines for 525-line
systems and ITU-R BT.1700's 576 lines for 625-line systems.

These numbers are not positions within a stored row or frame — the decoder
translates them into the source's own coordinates. That matters horizontally,
because captures disagree about where a row is cut: an ld-decode or vhs-decode
``.tbc`` starts each row at 0H (the half-amplitude point of the falling edge of
line sync), while a subcarrier-locked capture such as ``ld-chroma-encoder
--sc-locked`` output starts it at the first digital blanking sample instead, a
few samples ahead of 0H. The same ``first_active_sample`` lands on the same
picture either way.

Line numbering runs field by field rather than down the raster, so the two
fields' numbers interleave in the woven frame. ``first_active_line`` names the
window's **topmost** line, which need not be the lower number: 525-line output
is bottom-field-first, because ST 170's active picture is field 1 lines 21-263
and field 2 lines 283-525, and since field 2 begins at line 264 its first
active line sits half a line *above* field 1's. Line 283 is therefore the
frame's top row and line 263 its bottom. 625-line output begins on a field 1
line and is top-field-first.

To crop tighter, set the bounds explicitly:

.. code-block:: python

    from vsanalog import decode_4fsc_video

    # ld-chroma-decoder's picture crop (the pre-libchromadec default)
    decode_4fsc_video(src, first_active_sample=9,
                      last_active_sample=768)  # NTSC, 760 wide
    decode_4fsc_video(src, first_active_sample=8,
                      last_active_sample=929)  # PAL, 922 wide

    # Analog active line (~52.66 us NTSC, ~52.0 us PAL); nominal, since line
    # blanking carries a few samples of tolerance
    decode_4fsc_video(src, first_active_sample=9,
                      last_active_sample=762)  # NTSC, 754 wide
    decode_4fsc_video(src, first_active_sample=7,
                      last_active_sample=928)  # PAL, 922 wide

    # Field 1's active picture only, on a 525-line source
    decode_4fsc_video(src, first_active_line=21, last_active_line=263)

A negative ``first_active_sample`` widens the window leftwards out of the
digital active line and into the line blanking before it, which is how to see
sync and color burst: on a 525-line source ``first_active_sample=-125`` starts
the window at 0H. Widening rightwards past the end of the digital active line
works on a 0H-cut source but not on a subcarrier-locked one, whose stored rows
end there; the call fails rather than wrap round to the front of the row.

Each bound is independent, so setting only one keeps the standard value for the
other three. The resolved window is reported on every frame as
``AnalogFirstActiveSample`` / ``AnalogLastActiveSample`` /
``AnalogFirstActiveLine`` / ``AnalogLastActiveLine``, in these same standards
coordinates. The frame is exactly that window and is never padded, so pixel
(0, 0) is ``AnalogFirstActiveSample`` of ``AnalogFirstActiveLine``. Add borders
downstream with :py:func:`std.AddBorders` if a codec needs particular dimensions.


.. _metadata-sidecars:

Metadata Sidecars
~~~~~~~~~~~~~~~~~
Each source signal file must have a corresponding metadata sidecar file with
the same base name. For ``.tbc`` sources that is a ``.db`` (SQLite) or
``.json`` file, ``.db`` taking precedence when both are present; CVBS sources
carry a ``.meta`` sidecar. Both TBC forms are read directly and neither is
converted or written back — no ``.db`` is created alongside a ``.json`` source.


Usage Examples
~~~~~~~~~~~~~~

Basic Composite Decode
^^^^^^^^^^^^^^^^^^^^^^

.. code-block:: python

    from vsanalog import decode_4fsc_video

    clip = decode_4fsc_video("/path/to/capture.tbc")

Y/C-Separated Sources
^^^^^^^^^^^^^^^^^^^^^
For S-Video or VHS color-under captures produced by vhs-decode:

.. code-block:: python

    clip = decode_4fsc_video("luma.tbc", "chroma.tbc")

Choosing a Decoder
^^^^^^^^^^^^^^^^^^
.. code-block:: python

    # Use the 3D adaptive comb filter for NTSC:
    clip = decode_4fsc_video("capture.tbc", decoder="ntsc3d")

    # Use the Transform PAL frequency-domain filter:
    clip = decode_4fsc_video("capture.tbc", decoder="transform3d")

    # Luma-only (monochrome) decode:
    clip = decode_4fsc_video("capture.tbc", decoder="mono")

Correcting Dropouts
^^^^^^^^^^^^^^^^^^^
.. code-block:: python

    # Basic dropout correction:
    clip = decode_4fsc_video("capture.tbc", dropout_correct=True)

    # Multi-source dropout correction with extra captures:
    clip = decode_4fsc_video(
        "capture1.tbc",
        dropout_correct=True,
        dropout_composite_or_luma_extra_sources=[
            "capture2.tbc",
            "capture3.tbc",
        ],
    )

Working with the Output
^^^^^^^^^^^^^^^^^^^^^^^
The resulting ``YUV444PS`` format retains maximum quality from the decode but is
uncommon. You will often want to convert it for downstream filters:

.. code-block:: python

    import vapoursynth as vs

    # R'G'B' for color/levels adjustment filters:
    workable_clip = clip.resize.Point(format=vs.RGBS)

    # For filters built on the classic mvtools plugin, which needs an integer
    # format (vs-jetpack's QTGMC no longer does, taking YUV444PS as is):
    workable_clip = clip.resize.Point(format=vs.YUV444P16)

    # Reducing chroma resolution closer to analog source aids some NR filters:
    workable_clip = clip.resize.Spline36(format=vs.YUV422P16)


.. _dropouts:

Dropouts
--------
Signal dropouts flagged in a capture's metadata can be handled two ways, both
switches on :py:func:`vsanalog.decode_4fsc_video`: ``dropout_correct`` conceals
them during the decode, and ``annotate_dropouts`` reports them on the decoded
frames for the functions below to work with.

.. _dropout-correction:

Correction
~~~~~~~~~~
Decoding with ``dropout_correct=True`` replaces signal dropout regions
identified in the TBC metadata with data from nearby clean lines, using the
algorithm libchromadec carries over from `ld-decode-tools
<https://github.com/simoninns/ld-decode-tools>`_' ld-dropout-correct. Luma
and chroma are sourced independently using FIR frequency separation to find
the closest match for each.

For multi-source correction, pass additional TBC captures of the same content
via ``dropout_composite_or_luma_extra_sources``. For Y/C-separated formats, also
list each capture's chroma file at the same position in
``dropout_chroma_extra_sources`` to correct the chroma plane from those captures
too; the two lists must then be the same length. Sources are aligned
using VBI frame numbers when available (laserdisc CAV/CLV), falling back to
sequential frame alignment for sources without VBI data (e.g. vhs-decode
output).

When dropout correction is enabled, the following frame properties are set on
each output frame:

.. list-table::
    :header-rows: 1
    :widths: 35 10 55

    * - Property
      - Type
      - Description
    * - ``AnalogDropoutsCorrected``
      - int
      - Dropout regions successfully replaced
    * - ``AnalogDropoutsFailed``
      - int
      - Dropout regions where no replacement was found
    * - ``AnalogDropoutsTotalDistance``
      - int
      - Sum of line distances for all replacements


.. _dropout-annotation:

Annotation
~~~~~~~~~~
Correction *hides* dropouts. Decoding with ``annotate_dropouts=True`` instead
reports where they are, so another filter can act on them — in-painting the damage with a
plugin of your choosing rather than borrowing from neighbouring lines, say.

Annotation is independent of correction: annotate alone to hand the damaged
regions to something else, or alongside ``dropout_correct=True`` to see what was
concealed. Note the regions describe *detected* damage, so with correction on
they mark what has already been repaired, not what still needs repairing.
``dropout_overcorrect=True`` widens what gets reported to the same footprint it
widens correction to.

Because the regions ride along as frame properties, they survive ``std.Trim``,
``std.Interleave`` and splicing — the mask stays aligned whether you build it
before or after editing the decoded clip.

The property itself, ``AnalogDropoutSpans``, is a flat integer array holding
four values per region — ``y``, ``x_start``, ``x_end``, ``origin`` — in the
decoded clip's own pixel coordinates, with ``x`` half-open and regions sorted by
``y`` then ``x_start``. One array rather than four parallel ones keeps its
length off 1, which VapourSynth's Python layer would hand back as a bare int
instead of a list. It is present but empty on a frame with no dropouts, which
is what distinguishes an annotated clip from an unannotated one.
:py:func:`vsanalog.dropout_spans` unpacks it into
:py:class:`~vsanalog.DropoutSpan` records so you needn't.

.. autofunction:: vsanalog.create_dropouts_mask

.. autofunction:: vsanalog.dropout_spans

.. autoclass:: vsanalog.DropoutSpan

.. autoclass:: vsanalog.DropoutOrigin
    :members:

.. code-block:: python

    import vapoursynth as vs
    import vsanalog

    # Locate the dropouts without concealing them, then repair them elsewhere.
    clip = vsanalog.decode_4fsc_video(
        "capture.tbc", dropout_correct=False, annotate_dropouts=True)
    mask = vsanalog.create_dropouts_mask(clip)
    repaired = vs.core.std.MaskedMerge(clip, inpainted, mask)

The mask is a full-size single-plane clip matching the decoded clip's precision,
which is exactly what ``MaskedMerge`` wants — including for SECAM's 4:4:0
output, where it resamples the mask for the half-height chroma planes itself
rather than needing a pre-subsampled one. That resampling is bilinear, so a
one-line dropout softens across two chroma rows; ``std.Binarize`` the result if
you need the edges kept hard.

Grow the mask to cover the ringing either side of a dropout, or measure damage
per frame without repairing anything:

.. code-block:: python

    grown = vs.core.std.Maximum(mask).std.Inflate()

    with clip.get_frame(0) as f:
        spans = vsanalog.dropout_spans(f)
    damaged_samples = sum(s.x_end - s.x_start for s in spans)

On SECAM, ``dropout_spans`` also reports the FM click concealment the decoder
performed itself, tagged
:py:attr:`~vsanalog.DropoutOrigin.DECODER_CONCEALMENT` rather than
:py:attr:`~vsanalog.DropoutOrigin.SOURCE_METADATA`. Nothing upstream flagged
those regions — they exist only because the frame was decoded — so masking them
alone shows exactly which chroma samples were replaced rather than received:

.. code-block:: python

    concealed = vsanalog.create_dropouts_mask(
        clip, origins=[vsanalog.DropoutOrigin.DECODER_CONCEALMENT])


.. _colorimetry:

Colorimetry
-----------
Analog-era color often lives in chromaticities and transfer characteristics
that modern playback systems don't speak natively, and at whatever strength the
decoder gave it. ``modernize_chromaticity`` converts the colorimetry and
``amplify_chroma`` adjusts the strength, both on any clip rather than only a
fresh decode: a conventional capture loaded through a source plugin such as
BestSource is as valid an input as either.

``modernize_chromaticity`` converts to a modern target (BT.709, sRGB,
BT.2100 PQ/HLG, BT.2020 SDR) in one color-managed step. It performs no
geometry conversions and no dithering, so feed it high bit depth (e.g. the
32-bit float ``decode_4fsc_video`` produces) and dither at the end of your
pipeline. Subsampled Y'CbCr input is fine: chroma is upsampled through the
:external+vapoursynth:doc:`resize <functions/video/resize>` plugin for the
conversion (``resample_filter_uv``) and returned to
the source's subsampling on output.

Unlike the ``resize`` functions whose parameter names it borrows, the
``*_in`` parameters *override* frame properties; anything not given is
inferred from the clip's ``_Primaries``/``_Transfer``/``_Matrix`` properties,
and the filter errors when neither source is available. This is because the
IEC/ITU code points used by those properties don't always map to analog specs.

.. py:function:: vsanalog.modernize_chromaticity(clip, \
        *, \
        primaries_in_s=None, \
        transfer_in_s=None, \
        matrix_in_s=None, \
        primaries_s=None, \
        transfer_s=None, \
        matrix_s=None, \
        output_preset=None, \
        resample_filter_uv=None, \
        filter_param_a_uv=None, \
        filter_param_b_uv=None, \
        chromatic_adaptation=False, \
        nominal_luminance=None, \
        contrast_in=None, \
        brightness_in=None, \
        contrast=None, \
        brightness=None)

    Convert analog-era colorimetry and photometry to a modern target.

    :param clip:
        Input clip: YUV or RGB, constant format, integer up to 16 bits or
        32-bit float.
    :type clip: :py:class:`VideoNode`

    :param str primaries_in_s:
        Input chromaticity (CIE 1931 primaries + white point). Aliases in
        parentheses are equivalent. Broadcast systems:

        * ``"ntsc-1953"`` (``"bt470m"``, ``"470m"``, ``"fcc"``) — the 1953
          NTSC chromaticity, Illuminant C white
        * ``"bt470-japan"`` (``"470m93"``, ``"ntscj"``) — NTSC-1953 primaries
          with the Japanese D93 white
        * ``"bt1700-japan"`` (``"170j"``) — SMPTE C primaries with the
          Japanese white
        * ``"pal"`` (``"ebu"``, ``"bbc"``, ``"470bg"``) — the 625-line EBU
          chromaticity
        * ``"smpte-c"`` (``"st170"``, ``"170m"``) — SMPTE RP 145 / ST 170
        * ``"studio-japan"`` — ARIB TR-B9 pre-1996 Japanese studio practice
        * ``"nederland-proposal"`` — the CCIR Doc. XI/194 compromise primaries
        * ``"code-point-22"`` — the mystery H.273 chromaticity code point 22
          (neither JEDEC P22 nor EBU Tech 3213)

        CRT phosphor sets:

        * ``"ecia-xxa"`` (``"p22"``) through ``"ecia-xxg"`` — the seven
          registered phosphor sets of ECIA/TEPAC/JEDEC group XX ("P22"), in
          registration order; ``"ecia-xxa"`` is RCA's 1954 original
        * ``"rca-sulfide-8500k"``, ``"rca-sulfide-9300k-27mpcd"``,
          ``"rca-sulfide-c"`` — RCA's all-sulfide commercial mix under three
          assumed white points
        * ``"rca-p22-4-67"``, ``"rca-p22-5-61"``, ``"rca-p22-9-65"`` —
          phosphor sets from RCA's own P22 taxonomy
        * ``"sony-p22"`` — Sony's CRT phosphor set (Japanese white)

        Japanese entries use ITU-R BT.2035's reference D93 white point.
        Inferred from ``_Primaries`` when omitted.

    :param str transfer_in_s:
        Input transfer characteristics:

        * ``"linear"``
        * ``"ntsc-1953"`` (``"bt470m"``, ``"470m"``, ``"fcc"``, ``"gamma22"``)
          — assumed 2.2-gamma display
        * ``"bt470bg"`` (``"470bg"``, ``"tube"``, ``"gamma28"``) — assumed
          2.8-gamma display
        * ``"st170-scene"`` (``"st170-oetf"``, ``"bt601"``, ``"601"``) —
          scene-referred inverse of the ST 170 / BT.601 camera OETF
        * ``"st170-display"`` (``"st170-eotf"``) — ST 170's reference
          reproducer EOTF; linearizes identically to ``"st170-scene"``, the two
          names documenting intent
        * ``"bt1886-annex-1"`` (``"1886"``, ``"lcd"``, ``"gamma24"``) — the
          BT.1886 reference EOTF; honours ``contrast_in``/``brightness_in``
        * ``"bt1886-appendix-1"`` (``"1886a"``, ``"crt"``) — BT.1886's
          informative CRT-matching EOTF; honours
          ``contrast_in``/``brightness_in``
        * ``"srgb"`` (``"iec-61966-2-1"``)

        Inferred from ``_Transfer`` when omitted: 1 and 6 →
        ``"bt1886-annex-1"``, 4 → 2.2 gamma, 5 → 2.8 gamma, 8 → ``"linear"``,
        13 → ``"srgb"``. BT.709's reference EOTF is BT.1886, and
        ``_Transfer=6`` (SMPTE ST 170) deliberately gets the same reading
        rather than ``"st170-display"``: ST 170's idealized inverse-OETF
        reproducer was rarely what displays actually did — the era's displays
        were CRTs, the response BT.1886's Annex 1 EOTF was later written to
        approximate. This matches how ``resize``/zimg linearize code 6. Set
        ``transfer_in_s="st170-display"`` explicitly for a literal ST 170
        display, or ``"crt"`` (BT.1886 Appendix 1) for a closer CRT match.

    :param str matrix_in_s:
        Input Y'CbCr matrix (not applicable to RGB input):

        * ``"analog-classic"`` (``"ntsc-1953"``, ``"fcc"``) — the 0.30/0.11
          luma weights of NTSC-1953
        * ``"analog-modern"`` (``"bt470"``, ``"bt1700"``, ``"st170"``,
          ``"170m"``, ``"bt601"``, ``"601"``) — the 0.299/0.114 weights

        Inferred from ``_Matrix`` when omitted: 4 → classic, 5/6 → modern.

    :param str primaries_s:
        Output chromaticity:

        * ``"bt709"`` (``"709"``)
        * ``"bt2020"`` (``"2020"``)
        * ``"p3dci"`` (``"st431-2"``)
        * ``"p3d65"`` (``"st432-1"``)
        * ``"xyz"`` (``"st428"``) — CIE XYZ tristimulus output; requires
          ``matrix_s="rgb"``

    :param str transfer_s:
        Output transfer characteristics:

        * ``"linear"``
        * ``"bt1886-annex-1"`` (``"1886"``, ``"lcd"``, ``"gamma24"``) —
          honours ``contrast``/``brightness``; tagged as BT.709, or as the
          BT.2020 10/12-bit tag when paired with BT.2020 primaries
        * ``"srgb"`` (``"iec-61966-2-1"``)
        * ``"pq"`` (``"st2084"``, ``"2084"``)
        * ``"hlg"`` (``"std-b67"``) — display-referred, BT.2100 1.2-power OOTF

    :param str matrix_s:
        Output matrix:

        * ``"rgb"`` — RGB color family output
        * ``"bt709"`` (``"709"``)
        * ``"bt2020ncl"`` (``"bt2100"``, ``"2020ncl"``, ``"2020"``, ``"2100"``)
        * ``"2020cl"`` — BT.2020 constant luminance (H.273 matrix 10);
          requires ``bt2020`` primaries
        * ``"chromacl"`` (``"chromaticity-derived-cl"``) — constant luminance
          with the luma weights derived from ``primaries_s`` (H.273 matrix 13),
          so it pairs with any output chromaticity

        Required for YUV output unless ``output_preset`` supplies it; RGB input
        defaults to RGB output.

        Constant luminance is not a transfer characteristic: it applies the
        matrix to the linear tristimulus and the transfer curve *after* it,
        rather than before, so luminance survives chroma subsampling. Its
        color-difference normalizers are derived from whichever ``transfer_s``
        is in play, so the two are independent; naming no transfer falls back
        to the BT.2020 OETF, the pairing BT.2020's own table tabulates.

    :param str output_preset:
        Bundles the three output parameters:

        * ``"hdtv"`` (``"bt709"``) — BT.709 primaries, BT.1886 Annex 1
          transfer, BT.709 matrix
        * ``"uhdtv"`` (``"bt2100-pq"``) — BT.2020 primaries, PQ transfer,
          BT.2020 NCL matrix
        * ``"bt2100-hlg"`` — BT.2020 primaries, HLG transfer, BT.2020 NCL
          matrix
        * ``"bt2020-sdr"`` — BT.2020 primaries, BT.1886 Annex 1 transfer,
          BT.2020 NCL matrix
        * ``"srgb"`` (``"iec-61966-2-1"``) — BT.709 primaries, sRGB transfer,
          RGB output (the standard defines no matrix; add ``matrix_s`` for
          Y'CbCr)

        Explicit ``primaries_s``/``transfer_s``/``matrix_s`` override preset
        members, so ``matrix_s="rgb"`` combined with a preset keeps its
        colorimetry but yields RGB.

    :param str resample_filter_uv:
        Kernel for the internal chroma round trip on subsampled input, by the
        same names :py:func:`vsanalog.resample_secam` accepts: ``"point"``,
        ``"bilinear"``, ``"bicubic"`` (default), ``"spline16"``,
        ``"spline36"``, ``"spline64"``, or ``"lanczos"``. The upsample sites
        against the frame's ``_ChromaLocation``, and Y'CbCr output is returned
        to the input subsampling with the same kernel. Unused for 4:4:4 and
        RGB input. 4:4:0 input is rejected: SECAM from
        :py:func:`vsanalog.decode_4fsc_video` carries a line-sequential Db/Dr
        lattice that plain resampling would blend — realign it with
        :py:func:`vsanalog.resample_secam`, or
        :py:func:`vsanalog.fill_secam_by_delay` for the classic delay-line
        treatment.

    :param float filter_param_a_uv:
    :param float filter_param_b_uv:
        Kernel tuning for ``resample_filter_uv``, as in the
        :external+vapoursynth:doc:`resize <functions/video/resize>`
        functions: the bicubic b/c coefficients, or the lanczos tap count
        (``filter_param_a_uv`` only).

    :param bool chromatic_adaptation:
        Apply a Bradford chromatic adaptation between the input and output
        white points. Off by default (matching ``resize`` behaviour): whites
        keep their original tint, e.g. a D93-mastered picture stays cool on a
        D65 display. Beware that off, extremes can clip unintentionally in SDR
        output: full-strength white under a non-D65 input white lands outside
        the output's unit RGB range (NTSC-1953's Illuminant C white overshoots
        BT.709's red and blue by roughly 5-10% linear), clamping in integer
        output and deferring the clip downstream in float. PQ and HLG have
        headroom above SDR reference white and are unaffected. Enable
        adaptation — or attenuate first — when unclipped SDR highlights
        matter.

    :param float nominal_luminance:
        Physical luminance in cd/m² that linear 1.0 (SDR reference white) maps
        to for PQ and HLG output. Defaults to 100.

    :param float contrast_in:
    :param float brightness_in:
        Input-side BT.1886 user controls as 0.0-1.0 fractions of reference
        white: ``contrast_in`` is the screen white luminance L\ :sub:`W` (Annex
        1 user gain; also normalizes Appendix 1) and ``brightness_in`` the black
        lift (Annex 1 L\ :sub:`B`, Appendix 1 ``b``). Defaults 1.0 and 0.0.
        Only valid with the ``bt1886-annex-1``/``bt1886-appendix-1`` input
        transfers.

    :param float contrast:
    :param float brightness:
        Output-side counterparts; only valid with the ``bt1886-annex-1``
        output transfer.

    :rtype: :py:class:`VideoNode`

    There are no ``range``/``range_in`` parameters: input range is read from
    the ``_ColorRange`` frame property (defaulting to studio range for Y'CbCr
    and full range for RGB), and output range follows the output matrix's
    convention the same way.

    Output frames are tagged with the matching ``_Matrix``, ``_Transfer``,
    ``_Primaries``, ``_ColorRange`` and ``_Range`` properties.
    ``_ChromaLocation`` is preserved when the output keeps the input's
    subsampling and removed otherwise (siting is moot at 4:4:4). The BT.1886
    output transfer is signalled as BT.709 (``_Transfer=1``), or as the BT.2020
    10/12-bit OETF tag when paired with BT.2020 primaries, since H.273 has no
    display-side code point for it.

.. code-block:: python

    import vapoursynth as vs
    from vapoursynth import core

    import vsanalog

    clip = vsanalog.decode_4fsc_video("capture.tbc")

    # The decoded clip carries assumed colorimetry in frame properties.
    # When correct, a preset might be all that's needed:
    hd = vsanalog.modernize_chromaticity(clip, output_preset="hdtv")

    # Or override what the properties can't convey:
    hdr = vsanalog.modernize_chromaticity(
        clip,
        primaries_in_s="studio-japan",
        transfer_in_s="crt",
        output_preset="bt2100-pq",
    )
    # Take to a delivery format after filtering the modernized clip:
    hdr10 = core.resize.Spline36(
        hdr,
        format=vs.YUV420P10,
        dither_type="error_diffusion"
    )

    # Conventional capture? Serve from a conventional source plugin but raise
    # bit depth so we can dither when returning to lower bit depth.
    src = core.bs.VideoSource("capture.mkv")
    edit_fmt = src.format.replace(bits_per_sample=16)
    editable = src.resize.Point(format=edit_fmt)
    wide_gamut_sdr = vsanalog.modernize_chromaticity(
        editable,
        output_preset="bt2020-sdr"
    )
    output = wide_gamut_sdr.resize.Spline36(
        format=src.format,
        dither_type="random"
    )
    # Re-tag for lower bit-depth (BT.2020-specific):
    output = output.std.SetFrameProps(_Transfer=vs.TRANSFER_BT2020_10)

.. py:function:: vsanalog.amplify_chroma(clip, gain, \
        *, \
        resample_filter_uv=None, \
        filter_param_a_uv=None, \
        filter_param_b_uv=None)

    Amplify or attenuate the color-difference signals: a post-decode
    counterpart of :py:func:`vsanalog.decode_4fsc_video`'s ``chroma_gain``, which
    scales the demodulated color differences on their way out of the decoder.

    A saturation control, but saturation in analog video domain terms (a gain
    on E'Cb/E'Cr rather than the saturation axis of an HSV or HLS model).
    Frames with analog-style color difference planes are scaled in place; the
    rest are converted through a ``_Matrix=6`` intermediate and back.
    Colorimetry from outside the analog era is rejected — a frame's
    ``_Primaries`` must be 4 (NTSC-1953), 5 (EBU), 6 (SMPTE ST 170) or 7
    (SMPTE ST 240), or absent or ``unspecified`` (2), untagged captures being
    taken at their word — so this belongs upstream of
    :py:func:`vsanalog.modernize_chromaticity` rather than after it.

    :param clip:
        Input clip: YUV or RGB, constant format. GRAY carries no chroma and is
        rejected.
    :type clip: :py:class:`VideoNode`

    :param float gain:
        Multiplier applied to both color differences. Above ``1.0``
        amplifies and below ``1.0`` attenuates; ``0.0`` leaves a monochrome
        picture, and ``1.0`` hands the clip straight back without the
        conversion round trip a unity gain could only lose precision to. Must
        be ``0.0`` or greater.

    :param str resample_filter_uv:
        Kernel for the chroma resampling a matrix change entails, by the same
        names :py:func:`vsanalog.resample_secam` accepts: ``"point"``,
        ``"bilinear"``, ``"bicubic"`` (default), ``"spline16"``,
        ``"spline36"``, ``"spline64"``, or ``"lanczos"``. Only subsampled
        frames on non-analog axes use it — the matrix change happens at 4:4:4
        and is resampled back to the source subsampling, sited against the
        frame's ``_ChromaLocation``. Frames already carrying analog color
        differences never reach it, in any format or subsampling.

        4:4:0 frames needing that matrix change are refused rather than
        resampled: SECAM from :py:func:`vsanalog.decode_4fsc_video` carries a
        line-sequential Db/Dr lattice that plain resampling would blend, so
        realign it with :py:func:`vsanalog.resample_secam` (or
        :py:func:`vsanalog.fill_secam_by_delay`) first. Analog-matrix 4:4:0 is
        amplified normally, integer included, and needs no realignment.

    :param float filter_param_a_uv:
    :param float filter_param_b_uv:
        Kernel tuning for ``resample_filter_uv``, as in the
        :external+vapoursynth:doc:`resize <functions/video/resize>`
        functions: the bicubic b/c coefficients, or the lanczos tap count
        (``filter_param_a_uv`` only).

    :rtype: :py:class:`VideoNode`

    Output keeps the input's format, dimensions and frame properties. Integer
    output is rounded without dithering and clamps at the format's bounds, so
    amplifying an already saturated picture can flatten its most colorful
    areas; float clips nothing.

.. code-block:: python

    import vsanalog

    clip = vsanalog.decode_4fsc_video("capture.tbc")

    # A washed-out capture, given back some color before anything else
    # touches it:
    livelier = vsanalog.amplify_chroma(clip, 1.2)
    hd = vsanalog.modernize_chromaticity(livelier, output_preset="hdtv")

    # SECAM's 4:4:0 needs no realignment for a per-sample gain, so this is
    # safe before resample_secam:
    secam = vsanalog.decode_4fsc_video("secam.tbc", "secam_chroma.tbc",
                                       decoder="secam")
    calmer = vsanalog.resample_secam(vsanalog.amplify_chroma(secam, 0.85),
                                     format=vs.YUV444PS)


.. _secam-chroma:

SECAM Chroma
------------
SECAM carries one color-difference component per line, so a SECAM decode comes
out as ``YUV440PS`` (4:4:0) with each chroma plane holding only the lines its
component was really decoded from. Because the second field of a 625-line frame
sits an odd line count after the first, the components pair up in frame-row
order — ``Db, Dr, Dr, Db, Db, ...`` — so neither plane is a fixed-step lattice,
and which plane starts the frame flips frame to frame (the four-field ident
cycle of Rec. ITU-R BR.469, reported per frame as
``AnalogSecamFirstRowComponent``).

The chroma planes are woven by row parity the way the luma plane is, so
separating fields selects the same field on all three planes. A plane is still
not a picture, though: on any frame one of the two carries each adjacent row
pair spatially swapped, and which plane that is alternates.

These two functions turn the lattice into a conventional raster. Both read the
ident per frame, and both want woven frames — they split the fields themselves,
so an already-separated clip is rejected rather than silently mishandled. Run
them before deinterlacing, so the chroma is aligned before anything resamples
it vertically.

.. autofunction:: vsanalog.resample_secam

.. autofunction:: vsanalog.fill_secam_by_delay

.. code-block:: python

    import vsanalog

    clip = vsanalog.decode_4fsc_video("secam.tbc", "secam_chroma.tbc",
                                      decoder="secam")

    # Resample the lattice — truest to the decoded samples.
    resampled = vsanalog.resample_secam(clip, format=vs.YUV444PS)

    # Or reproduce what a receiver's delay line showed, copying each line's
    # missing component from the previous line of the same field.
    as_broadcast = vsanalog.fill_secam_by_delay(clip)

    # Deinterlace afterwards, never before.
    from vsdeinterlace import QTempGaussMC
    progressive = QTempGaussMC().deinterlace(resampled)


Utility
-------
.. autofunction:: vsanalog.requires_plugin

.. autofunction:: vsanalog.set_log_level
