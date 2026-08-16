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

    Returns a 32-bit float clip whose format depends on *color_family*:
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
        Path to the Pr component ``.tbc`` file (component video).
    :type pr_source: :py:class:`str` | :py:class:`~pathlib.Path` | None

    :param decoder:
        Chroma decoder to use. Analytical: ``"ntsc1d"``, ``"ntsc2d"``,
        ``"ntsc3d"``, ``"ntsc3dnoadapt"``, ``"pal2d"``, ``"transform2d"``,
        ``"transform3d"``, ``"secam"``, or ``"mono"``. Neural-network
        (NTSC only): ``"nntransform3d"``, ``"ldzeug2_color_cnn"``,
        ``"ldzeug2_luma_sep"``, ``"ldzeug2_luma_sep_frame"``. When *None*, the
        decoder is chosen automatically based on the video system.
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
        Bundled model to use for a neural-network *decoder* (defaults per
        decoder). Ignored when *model_path* is given. Choices:
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
        macOS) for a neural-network *decoder*.
    :type model_path: :py:class:`str` | :py:class:`~pathlib.Path` | None

    :param model_input_scale:
        Override the model input magnitude divisor (``nntransform3d`` only).
    :type model_input_scale: :py:class:`float` | None

    :param model_precision:
        Compute precision a neural-network *decoder*'s backend may use:
        ``"fp32"`` or ``"fp16"``. Permission rather than a guarantee. TensorRT
        acts on it by building a mixed fp16/fp32 engine from the bundled fp32
        weights (fp16 and fp32 engines are cached separately, so changing this
        never reuses an engine built the other way). CUDA and DirectML have no
        such engine mode, so on those the wheel instead bundles a pre-converted
        fp16 copy of the weights, used only when *onnx_provider* is pinned to
        ``"cuda"`` or ``"directml"`` — an ``"auto"`` request that lands on the
        CUDA provider stays fp32. Every other backend ignores the setting.
        Defaults to ``"fp16"`` for the bundled ``nntransform3d`` ``"v2"``
        weights and ``"fp32"`` for everything else, including any custom
        *model_path* (nothing here can inspect an arbitrary model's training
        scale, and fp16 on weights that overflow it yields NaN output).
        Unrelated to the macOS packages' precision, which is fixed when they
        are converted.
    :type model_precision: :py:class:`str` | None

    :param onnx_provider:
        Execution provider for a neural-network *decoder*: ``"auto"``,
        ``"cpu"``, ``"cuda"``/``"gpu"``, ``"tensorrt"``/``"trt"``,
        ``"migraphx"``, ``"directml"``, or ``"coreml"``. An unavailable
        accelerator falls back to CPU.
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
        4𝑓𝑠𝑐 sample grid. Set to *False* to force fixed-phase demodulation.
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
        Enable dropout correction using metadata-identified dropouts.

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
        *dropout_correct*; *dropout_overcorrect* widens what gets reported.
        See :ref:`dropout-annotation-py`.

    :param dropout_composite_or_luma_extra_sources:
        Additional composite or luma ``.tbc`` files for multi-source dropout
        correction.
    :type dropout_composite_or_luma_extra_sources: :py:class:`~collections.abc.Sequence`\[:py:class:`str` | :py:class:`~pathlib.Path`] | None

    :param dropout_chroma_extra_sources:
        Additional chroma ``.tbc`` files for multi-source dropout correction
        (for color-under formats).
    :type dropout_chroma_extra_sources: :py:class:`~collections.abc.Sequence`\[:py:class:`str` | :py:class:`~pathlib.Path`] | None

    :rtype: :py:class:`VideoNode`

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

Dropout Correction
^^^^^^^^^^^^^^^^^^
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

    # For mvtools2 pipelines like QTGMC that require an integer format:
    workable_clip = clip.resize.Point(format=vs.YUV444P16)

    # Reducing chroma resolution closer to analog source aids some NR filters:
    workable_clip = clip.resize.Spline36(format=vs.YUV422P16)


.. _dropout-annotation-py:

Dropouts
--------
Correction *hides* dropouts. ``annotate_dropouts=True`` instead reports where
they are, so another filter can act on them — in-painting the damage with a
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
        Input chromaticity. Broadcast systems: ``"ntsc-1953"``
        (``"bt470m"``/``"470m"``/``"fcc"``), ``"bt470-japan"``
        (``"470m93"``/``"ntscj"``), ``"bt1700-japan"`` (``"170j"``), ``"pal"``
        (``"ebu"``/``"bbc"``/``"470bg"``), ``"smpte-c"``
        (``"st170"``/``"170m"``), ``"studio-japan"``, ``"nederland-proposal"``,
        ``"code-point-22"`` (the mystery H.273 chromaticity). CRT phosphor
        sets: ``"ecia-xxa"`` (``"p22"``) through ``"ecia-xxg"``,
        ``"rca-sulfide-8500k"``, ``"rca-sulfide-9300k-27mpcd"``,
        ``"rca-sulfide-c"``, ``"rca-p22-4-67"``, ``"rca-p22-5-61"``,
        ``"rca-p22-9-65"``, ``"sony-p22"``. Japanese entries use ITU-R
        BT.2035's reference D93 white point. Inferred from ``_Primaries`` when
        omitted.

    :param str transfer_in_s:
        Input transfer characteristics: ``"linear"``, ``"ntsc-1953"``
        (``"bt470m"``/``"470m"``/``"fcc"``/``"gamma22"``), ``"bt470bg"``
        (``"470bg"``/``"tube"``/``"gamma28"``), ``"st170-scene"``
        (``"st170-oetf"``/``"bt601"``/``"601"``), ``"st170-display"``
        (``"st170-eotf"``), ``"bt1886-annex-1"``
        (``"1886"``/``"lcd"``/``"gamma24"``), ``"bt1886-appendix-1"``
        (``"1886a"``/``"crt"``), or ``"srgb"`` (``"iec-61966-2-1"``). Inferred
        from ``_Transfer`` when omitted.

    :param str matrix_in_s:
        Input Y'CbCr matrix: ``"analog-classic"`` (``"ntsc-1953"``/``"fcc"``,
        the 0.30/0.11 luma weights) or ``"analog-modern"``
        (``"bt470"``/``"bt1700"``/``"st170"``/``"170m"``/``"bt601"``/``"601"``,
        the 0.299/0.114 weights). Not applicable to RGB input. Inferred from
        ``_Matrix`` when omitted.

    :param str primaries_s:
        Output chromaticity: ``"bt709"`` (``"709"``), ``"bt2020"``
        (``"2020"``), ``"p3dci"`` (``"st431-2"``), ``"p3d65"``
        (``"st432-1"``), or ``"xyz"`` (``"st428"``; requires
        ``matrix_s="rgb"``).

    :param str transfer_s:
        Output transfer characteristics: ``"linear"``, ``"bt1886-annex-1"``
        (``"1886"``/``"lcd"``/``"gamma24"``; tagged as BT.709, or as the
        BT.2020 10/12-bit tag when paired with BT.2020 primaries), ``"srgb"``
        (``"iec-61966-2-1"``), ``"pq"`` (``"st2084"``/``"2084"``), or
        ``"hlg"`` (``"std-b67"``).

    :param str matrix_s:
        Output matrix: ``"rgb"`` (produces an RGB clip), ``"bt709"``
        (``"709"``), ``"bt2020ncl"``
        (``"bt2100"``/``"2020ncl"``/``"2020"``/``"2100"``), ``"2020cl"``
        (BT.2020 constant luminance), or ``"chromacl"``
        (``"chromaticity-derived-cl"``; constant luminance with the luma
        weights derived from *primaries_s*, so it pairs with any output
        chromaticity). Required for YUV output unless *output_preset* supplies
        it; RGB input defaults to RGB output.

        Constant luminance is not a transfer characteristic: it applies the
        matrix to the linear tristimulus and the transfer curve *after* it,
        rather than before, so luminance survives chroma subsampling. Its
        color-difference normalizers are derived from whichever *transfer_s*
        is in play, so the two are independent; naming no transfer falls back
        to the BT.2020 OETF, the pairing BT.2020's own table tabulates.

    :param str output_preset:
        Convenience bundle: ``"hdtv"``/``"bt709"`` (BT.709 primaries, BT.1886
        transfer, BT.709 matrix), ``"uhdtv"``/``"bt2100-pq"`` (BT.2020
        primaries, PQ, BT.2020 NCL matrix), ``"bt2100-hlg"`` (BT.2020
        primaries, HLG, BT.2020 NCL matrix), ``"bt2020-sdr"``
        (BT.2020 primaries, BT.1886, BT.2020 NCL matrix), or
        ``"srgb"``/``"iec-61966-2-1"`` (BT.709 primaries, sRGB transfer, RGB
        output — the standard defines no matrix, so add *matrix_s* for
        Y'CbCr). Explicit *primaries_s*/*transfer_s*/*matrix_s* override
        preset members; ``matrix_s="rgb"`` keeps the preset's colorimetry but
        yields RGB.

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
        Kernel tuning for *resample_filter_uv*, as in the
        :external+vapoursynth:doc:`resize <functions/video/resize>`
        functions: the bicubic b/c coefficients, or the lanczos tap count
        (*filter_param_a_uv* only).

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
        white: *contrast_in* is the screen white luminance L\ :sub:`W` (Annex
        1 user gain; also normalizes Appendix 1) and *brightness_in* the black
        lift (Annex 1 L\ :sub:`B`, Appendix 1 ``b``). Defaults 1.0 and 0.0.
        Only valid with the ``bt1886-annex-1``/``bt1886-appendix-1`` input
        transfers.

    :param float contrast:
    :param float brightness:
        Output-side counterparts; only valid with the ``bt1886-annex-1``
        output transfer.

    :rtype: :py:class:`VideoNode`

    Output range follows the output matrix's convention (studio range for
    Y'CbCr, full range for RGB); input range is read from the ``_ColorRange``
    frame property with the same convention as the fallback.

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
    counterpart of :py:func:`vsanalog.decode_4fsc_video`'s *chroma_gain*, which
    scales the demodulated color differences on their way out of the decoder.

    A saturation control, but saturation in analog video domain terms (a gain
    on E'Cb/E'Cr rather than the saturation axis of an HSV or HLS model).
    Frames with analog-style color difference planes are scaled in place; the
    rest are converted through a ``_Matrix=6`` intermediate and back.
    Colorimetry from outside the analog era is rejected, so this belongs
    upstream of :py:func:`vsanalog.modernize_chromaticity` rather than after
    it.

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
        Kernel tuning for *resample_filter_uv*, as in the
        :external+vapoursynth:doc:`resize <functions/video/resize>`
        functions: the bicubic b/c coefficients, or the lanczos tap count
        (*filter_param_a_uv* only).

    :rtype: :py:class:`VideoNode`

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
    progressive = QTempGaussMC(resampled.resize.Point(format=vs.YUV444P16)).deinterlace()


Utility
-------
.. autofunction:: vsanalog.requires_plugin

.. autofunction:: vsanalog.set_log_level
