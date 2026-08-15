VapourSynth Plugin API
======================
The low-level VapourSynth plugin is registered under the ``analog`` namespace.
It can be called directly from VapourSynth scripts without the Python wrapper.

Decoding
--------

.. function:: core.analog.decode_4fsc_video(\
        composite_or_luma_source \
        [, chroma_or_pb_source] \
        [, pr_source] \
        [, decoder] \
        [, color_family] \
        [, color_difference_precision] \
        [, broadcast_scaling_precision] \
        [, model_path] \
        [, onnx_provider] \
        [, model_chroma_bandpass=1] \
        [, model_input_scale=1.0] \
        [, reverse_fields=0] \
        [, chroma_gain=1.0] \
        [, chroma_phase=0.0] \
        [, chroma_nr=0.0] \
        [, luma_nr=0.0] \
        [, phase_compensation=1] \
        [, first_active_sample] \
        [, last_active_sample] \
        [, first_active_line] \
        [, last_active_line] \
        [, dropout_correct=0] \
        [, dropout_overcorrect=0] \
        [, dropout_intra=0] \
        [, dropout_composite_or_luma_extra_sources] \
        [, dropout_chroma_extra_sources] \
        [, annotate_dropouts=0])

    Decodes 4𝑓𝑠𝑐 (four times subcarrier frequency) sampled analog video signals
    to a digital video clip. The signal data must be orthogonal video system
    lines with well-formed blanking and syncing structure at a stable time
    base, such as those produced by
    `ld-decode <https://github.com/happycube/ld-decode>`_ and
    `vhs-decode <https://github.com/oyvindln/vhs-decode>`_. These files
    normally have a ``.tbc`` extension indicating they are time-base-corrected
    and must have a metadata sidecar file in JSON or SQLite format. The newer
    CVBS format is also read, detected by extension: ``.cvbs``, or
    ``.cvbsy``/``.cvbsc`` for separated luma/chroma (the pre-1.5.0
    ``.composite`` and ``.y``/``.c`` spellings are still accepted). RAW CVBS
    encodings are rejected.

    Returns a 32-bit float clip: ``YUV444PS`` (default), ``RGBS``, ``GRAYS``, or
    ``YUV440PS`` for SECAM, per ``color_family``.

    Note: this is the low-level interface. Neural-network ``decoder`` values
    need an explicit ``model_path``; the :doc:`Python wrapper <python-api>`
    resolves bundled models by ``model_version`` for you.

    :param str composite_or_luma_source:
        Path to the composite or luma-only capture (``.tbc``/``.cvbs``).

    :param str chroma_or_pb_source:
        Path to a separate chroma ``.tbc`` file, for Y/C-separated sources such
        as S-Video or VHS color-under.

    :param str pr_source:
        Path to the Pr component ``.tbc`` file (component video, not yet
        supported).

    :param str decoder:
        Chroma decoder to use. See :ref:`decoder-options` below. When not
        specified, the decoder is chosen automatically based on the video
        system in TBC metadata. When ``chroma_or_pb_source`` is supplied, this
        decoder applies to the chroma TBC only; the luma TBC is read with the
        ``mono`` decoder so it isn't run through Y/C separation a second
        time. Add ``"secam"`` and the neural-network decoders
        (``"nntransform3d"``, ``"ldzeug2_color_cnn"``, ``"ldzeug2_luma_sep"``,
        ``"ldzeug2_luma_sep_frame"``, all NTSC only).

    :param str color_family:
        Output family: ``"yuv"`` (default), ``"rgb"``, or ``"gray"``. RGB is
        rejected for SECAM.

    :param str color_difference_precision:
        ``"classic"`` or ``"modern"``.

    :param str broadcast_scaling_precision:
        ``"classic"``, ``"modern"`` or ``"scientific"``.

    :param str model_path:
        Path to NN model weights (``.onnx``, or ``.mlpackage`` on macOS).
        Required for a neural-network ``decoder``.

    :param str onnx_provider:
        Execution provider: ``auto``, ``cpu``, ``cuda``/``gpu``,
        ``tensorrt``/``trt``, ``migraphx``, ``directml``, ``coreml``. Falls back
        to CPU when unavailable.

    :param str model_precision:
        Compute precision the backend may use: ``fp32`` (default) or ``fp16``.
        Only a backend that compiles the model into a device engine acts on it
        — TensorRT, which then builds mixed fp16/fp32 kernels and caches those
        engines separately. Set ``fp16`` only for weights whose input contract
        keeps every tensor in fp16 range; the Python wrapper knows which
        bundled models qualify and sets this for you.

    :param int model_chroma_bandpass:
        I/Q low-pass toggle for ``ldzeug2_luma_sep``/``ldzeug2_luma_sep_frame``
        (default ``1``).

    :param float model_input_scale:
        Input magnitude divisor for ``nntransform3d`` (default ``1.0``).

    :param int reverse_fields:
        Set to 1 to swap field order.

    :param float chroma_gain:
        Chroma gain multiplier for saturation adjustment. Default ``1.0``.

    :param float chroma_phase:
        Chroma phase adjustment in degrees. Default ``0.0``.

    :param float chroma_nr:
        Chroma noise-reduction level. Only applies to NTSC decoders.
        Default ``0.0``.

    :param float luma_nr:
        Luma noise-reduction level. Default ``0.0``.

    :param int phase_compensation:
        Burst-locked NTSC chroma demodulation, recovering the subcarrier phase
        from each line's colorburst instead of assuming it's locked to the
        4𝑓𝑠𝑐 sample grid. Set to 0 to force fixed-phase demodulation.
        Default ``1``. The PAL decoders are burst-locked by design and ignore
        this.

    :param int first_active_sample:
    :param int last_active_sample:
        Inclusive horizontal crop, in the sample numbering of the 4𝑓𝑠𝑐 interface
        standards: sample 0 is the first sample of the digital active line, and
        negative numbers reach back into the line blanking ahead of it (see
        :ref:`active-window` below). Defaults to the whole digital active line.

    :param int first_active_line:
    :param int last_active_line:
        Inclusive vertical crop, in the standards' field-sequential signal line
        numbers. ``first_active_line`` is the window's topmost line. Defaults to
        the video system's standard active picture.

    :param int dropout_correct:
        Set to 1 to enable dropout correction using metadata-identified
        dropouts. See :ref:`dropout-correction` below. Default ``0``.

    :param int dropout_overcorrect:
        Set to 1 to extend dropout boundaries by +/-24 samples. For heavily
        damaged sources. Default ``0``.

    :param int dropout_intra:
        Set to 1 to force intra-field-only correction, avoiding inter-field
        borrowing artifacts on high-motion content. Default ``0``.

    :param str[] dropout_composite_or_luma_extra_sources:
        Additional composite or luma ``.tbc`` files for multi-source dropout
        correction.

    :param int annotate_dropouts:
        Set to 1 to record each frame's dropout regions in the
        ``AnalogDropoutSpans`` frame property, for use with
        :ref:`create_dropouts_mask <create-dropouts-mask>`. Independent of
        ``dropout_correct``. See :ref:`dropout-annotation` below. Default ``0``.

    :param str[] dropout_chroma_extra_sources:
        Additional chroma ``.tbc`` files for multi-source dropout correction
        (for color-under formats).


Examples:

.. code-block:: python

    import vapoursynth as vs
    from vapoursynth import core

    # Basic composite decode:
    clip = core.analog.decode_4fsc_video("/path/to/capture.tbc")

    # Y/C-separated decode:
    clip = core.analog.decode_4fsc_video("luma.tbc", "chroma.tbc")


Dropouts
--------

.. _create-dropouts-mask:

.. function:: core.analog.create_dropouts_mask(clip[, origins])

    Rasterises a clip's annotated dropout regions into a mask clip.

    ``clip`` must have been decoded with ``annotate_dropouts=1``; a clip without
    the ``AnalogDropoutSpans`` property is an error. The result is a
    single-plane clip matching ``clip``'s dimensions and precision — ``0`` for
    clean samples, full scale (``1.0``, or ``2**bits - 1``) for dropped ones —
    which drops straight into ``core.std.MaskedMerge`` and the other ``std``
    mask functions.

    The mask is full-size even for subsampled clips, which is what
    ``MaskedMerge`` requires: it resamples the mask for the chroma planes
    itself, honouring the clip's ``_ChromaLocation``. For SECAM's 4:4:0 output
    that resampling is bilinear, so a one-line dropout softens across two chroma
    rows.

    Because the regions travel as frame properties, they survive ``std.Trim``,
    ``std.Interleave`` and splicing — build the mask before or after editing the
    decoded clip and it stays aligned either way.

    :param vnode clip:
        An annotated clip, 8-16 bit integer or 32-bit float.

    :param int[] origins:
        Restrict the mask to regions of particular origin: ``0`` for regions
        flagged in the source metadata, ``8`` for decoder concealment. Defaults
        to every origin. Masking ``origins=[8]`` alone shows exactly which
        chroma samples SECAM click concealment replaced.

Examples:

.. code-block:: python

    import vapoursynth as vs
    core = vs.core

    # Locate the dropouts without concealing them, then repair them elsewhere.
    clip = core.analog.decode_4fsc_video(
        "capture.tbc", dropout_correct=0, annotate_dropouts=1)
    mask = core.analog.create_dropouts_mask(clip)
    repaired = core.std.MaskedMerge(clip, inpainted, mask)

    # Grow the mask a little to cover the ringing at a dropout's edges.
    grown = core.std.Maximum(mask).std.Inflate()


Colorimetry
-----------

.. function:: core.analog.modernize_chromaticity(clip \
        [, primaries_in_s] \
        [, transfer_in_s] \
        [, matrix_in_s] \
        [, primaries_s] \
        [, transfer_s] \
        [, matrix_s] \
        [, output_preset] \
        [, resample_filter_uv] \
        [, filter_param_a_uv] \
        [, filter_param_b_uv] \
        [, chromatic_adaptation=0] \
        [, nominal_luminance=100.0] \
        [, contrast_in=1.0] \
        [, brightness_in=0.0] \
        [, contrast=1.0] \
        [, brightness=0.0])

    Converts analog-era colorimetry and photometry to a modern target,
    intended downstream of ``decode_4fsc_video`` or of a source plugin (such
    as BestSource) reading previously captured video. Color only — no
    geometry conversions and no dithering. Input is YUV or RGB (integer up
    to 16 bits, or 32-bit float). The color math always runs at 4:4:4:
    subsampled Y'CbCr input is upsampled through the
    :external+vapoursynth:doc:`resize <functions/video/resize>` plugin (see
    ``resample_filter_uv``), converted, and returned to its original
    subsampling on Y'CbCr output — the same round trip the ``resize``
    functions perform for colorimetry changes.

    Parameters share names with VapourSynth's ``resize`` functions where the
    meaning is similar, with one deliberate difference: the ``*_in``
    parameters here *override* frame properties. When an ``*_in`` parameter
    is omitted, the value is inferred from the matching frame property
    (``_Primaries``, ``_Transfer``, ``_Matrix``), and frame requests fail if
    the property is absent, ``unspecified``, or has no supported equivalent.

    Because there is no dithering, prefer high-bit-depth input and output —
    ideally the 32-bit float that ``decode_4fsc_video`` produces — and leave
    quantization to a proper dithering step at the end of the pipeline. There
    are likewise no ``range``/``range_in`` parameters: input range comes from
    the ``_ColorRange`` frame property (defaulting to studio range for
    Y'CbCr and full range for RGB), and output range follows the output
    matrix's convention the same way.

    To switch color family (YUV to RGB or the reverse), specify ``matrix_s``
    or an ``output_preset``; RGB input without either yields RGB output.

    :param vnode clip:
        Input clip: YUV 4:4:4 or RGB, constant format.

    :param str primaries_in_s:
        Input chromaticity (CIE 1931 primaries + white point). Options
        sharing a line are equivalent:

        * ``ntsc-1953``, ``bt470m``, ``470m``, ``fcc`` — the 1953 NTSC
          chromaticity (Illuminant C white)
        * ``bt470-japan``, ``470m93``, ``ntscj`` — NTSC-1953 primaries with
          the Japanese "D93" white
        * ``bt1700-japan``, ``170j`` — SMPTE C primaries with the Japanese
          white
        * ``pal``, ``ebu``, ``bbc``, ``470bg`` — the 625-line EBU
          chromaticity
        * ``smpte-c``, ``st170``, ``170m`` — SMPTE RP 145 / ST 170
        * ``code-point-22`` — the mystery IEC/ITU primaries code point 22
          (neither JEDEC P22 nor EBU Tech 3213)
        * ``ecia-xxa``, ``p22`` … ``ecia-xxg`` — the seven registered
          phosphor sets of ECIA/TEPAC/JEDEC group XX ("P22"), in
          registration order; ``xxa`` is RCA's 1954 original
        * ``rca-sulfide-8500k``, ``rca-sulfide-9300k-27mpcd``,
          ``rca-sulfide-c`` — RCA's all-sulfide commercial mix under three
          assumed white points (never published)
        * ``rca-p22-4-67``, ``rca-p22-5-61``, ``rca-p22-9-65`` — phosphor
          sets from RCA's own P22 taxonomy
        * ``sony-p22`` — Sony's CRT phosphor set (Japanese white; never a
          JEDEC/TEPAC/ECIA registration)
        * ``studio-japan`` — ARIB TR-B9 pre-1996 Japanese studio practice
        * ``nederland-proposal`` — the CCIR Doc. XI/194 compromise primaries

        All Japanese entries use ITU-R BT.2035's reference D93 white point.
        Inferred from ``_Primaries`` values 4, 5, 6/7 and 22 when omitted.

    :param str transfer_in_s:
        Input transfer characteristics:

        * ``linear``
        * ``ntsc-1953``, ``bt470m``, ``470m``, ``fcc``, ``gamma22`` — assumed
          2.2-gamma display
        * ``bt470bg``, ``470bg``, ``tube``, ``gamma28`` — assumed 2.8-gamma
          display
        * ``st170-scene``, ``st170-oetf``, ``bt601``, ``601`` — scene-referred
          inverse of the ST 170 / BT.601 camera OETF
        * ``st170-display``, ``st170-eotf`` — ST 170's reference reproducer
          EOTF (§5.2). This is the exact inverse of the camera OETF, so it
          linearizes identically to ``st170-scene``; the two names exist to
          document intent.
        * ``bt1886-annex-1``, ``1886``, ``lcd``, ``gamma24`` — the BT.1886
          reference EOTF; honours ``contrast_in``/``brightness_in``
        * ``bt1886-appendix-1``, ``1886a``, ``crt`` — BT.1886's informative
          CRT-matching EOTF; honours ``contrast_in``/``brightness_in``
        * ``srgb``, ``iec-61966-2-1``

        Inferred from ``_Transfer`` when omitted: 1 and 6 →
        ``bt1886-annex-1``, 4 → 2.2 gamma, 5 → 2.8 gamma, 8 → ``linear``,
        13 → ``srgb``. BT.709's reference EOTF is BT.1886, and ``_Transfer=6``
        (SMPTE ST 170) deliberately gets the same reading rather than
        ``st170-display``: ST 170's idealized inverse-OETF reproducer was
        rarely what displays actually did — the era's displays were CRTs,
        the response BT.1886's Annex 1 EOTF was later written to approximate
        so that flat-panel displays with digital processing could emulate it.
        This matches how ``resize``/zimg linearize code 6 (equivalent to
        code 1, display-referred BT.1886). Set
        ``transfer_in_s="st170-display"`` explicitly for a literal §5.2
        display, or ``"crt"`` (BT.1886 Appendix 1) for a closer CRT match.

    :param str matrix_in_s:
        Input Y'CbCr matrix (Y'CbCr input only):

        * ``analog-classic``, ``ntsc-1953``, ``fcc`` — the 0.30/0.11 luma
          weights of NTSC-1953
        * ``analog-modern``, ``bt470``, ``bt1700``, ``st170``, ``170m``,
          ``bt601``, ``601`` — the 0.299/0.114 weights

        Inferred from ``_Matrix`` when omitted: 4 → classic, 5/6 → modern.

    :param str primaries_s:
        Output chromaticity: ``bt709``/``709``, ``bt2020``/``2020``,
        ``p3dci``/``st431-2``, ``p3d65``/``st432-1``, or ``xyz``/``st428``
        (CIE XYZ tristimulus output; requires ``matrix_s="rgb"``).

    :param str transfer_s:
        Output transfer characteristics: ``linear``;
        ``bt1886-annex-1``/``1886``/``lcd``/``gamma24`` (honours
        ``contrast``/``brightness``); ``srgb``/``iec-61966-2-1`` (IEC
        61966-2-1 Equations 7 and 8); ``pq``/``st2084``/``2084``; or
        ``hlg``/``std-b67`` (display-referred, BT.2100 1.2-power OOTF).

    :param str matrix_s:
        Output matrix: ``rgb`` (RGB color family output),
        ``bt709``/``709``,
        ``bt2020ncl``/``bt2100``/``2020ncl``/``2020``/``2100``, or
        ``2020cl`` (BT.2020 constant luminance, H.273 matrix 10), or
        ``chromacl``/``chromaticity-derived-cl`` (H.273 matrix 13: constant
        luminance whose luma weights are derived from ``primaries_s`` per
        H.273 Equations E-22 to E-27, so it pairs with any output
        chromaticity — ``2020cl`` itself means BT.2020's weights by
        definition and so requires ``bt2020`` primaries).

        Constant luminance is not a transfer characteristic: BT.2020 Table 4
        gives CL and NCL the same OETF and differs only in where it is
        applied. NCL encodes each channel and then matrixes; CL matrixes the
        linear tristimulus and encodes the result, so luminance survives
        chroma subsampling intact. The color-difference normalizers
        (P\ :sub:`B`/N\ :sub:`B`/P\ :sub:`R`/N\ :sub:`R`) follow the transfer
        curve rather than fixing it — H.273 Equations E-62 to E-65 define
        them as the transfer characteristic function applied to expressions
        in K\ :sub:`B` and K\ :sub:`R` — so ``transfer_s`` stays free and is
        applied to the CL constants and the picture alike. BT.2020 Table 4's
        published numbers are that derivation under BT.2020's own OETF, which
        is what a CL matrix falls back to when ``transfer_s`` is unset.

        Pairing CL with the display-referred ``bt1886-annex-1`` is supported
        on the strength of H.273's note that the BT.709/BT.2020 OETF code
        points, though defined as an OETF, take BT.1886 as their
        corresponding reference EOTF. Be aware that decoders which read those
        code points strictly as the OETF (zimg does, treating CL as always
        scene-referred) will derive different normalizers than a
        display-referred flow.

    :param str output_preset:
        Bundles the three output parameters:

        * ``hdtv``, ``bt709`` — BT.709 primaries, BT.1886 Annex 1 transfer,
          BT.709 matrix
        * ``uhdtv``, ``bt2100-pq`` — BT.2020 primaries, PQ transfer,
          BT.2020 NCL matrix
        * ``bt2100-hlg`` — BT.2020 primaries, HLG transfer, BT.2020 NCL
          matrix
        * ``bt2020-sdr`` — BT.2020 primaries, BT.1886 Annex 1 transfer, BT.2020
          NCL matrix
        * ``srgb``, ``iec-61966-2-1`` — BT.709 primaries, sRGB transfer, RGB
          output (the standard defines no matrix; add ``matrix_s`` for Y'CbCr)

        Explicit ``primaries_s``/``transfer_s``/``matrix_s`` win over the
        preset, so ``matrix_s="rgb"`` combined with a preset keeps its
        colorimetry but emits RGB.

    :param str resample_filter_uv:
        Kernel for the internal chroma round trip on subsampled input:
        ``point``, ``bilinear``, ``bicubic`` (default, as in
        ``resample_secam``), ``spline16``, ``spline36``, ``spline64``, or
        ``lanczos``. The upsample sites against the frame's
        ``_ChromaLocation``; Y'CbCr output is brought back to the input
        subsampling with the same kernel, and ``_ChromaLocation`` is
        preserved. Unused for 4:4:4 and RGB input. 4:4:0 input is rejected:
        SECAM from ``decode_4fsc_video`` carries a line-sequential Db/Dr
        lattice that plain resampling would blend — realign it with
        ``resample_secam``, or ``fill_secam_by_delay`` for the classic
        delay-line treatment.

    :param float filter_param_a_uv:
    :param float filter_param_b_uv:
        Kernel tuning for ``resample_filter_uv``, as in the ``resize``
        functions: the bicubic b/c coefficients, or the lanczos tap count
        (``filter_param_a_uv`` only).

    :param int chromatic_adaptation:
        Set to 1 to apply a Bradford chromatic adaptation from the input
        white point to the output one. Default 0 (unlike some converters):
        whites keep their original tint, so e.g. a D93-mastered picture stays
        cool on a D65 display, as it looked in its era.

        Beware that with adaptation off, extremes can clip unintentionally in
        SDR output: a full-strength white under a non-D65 input white point
        lands outside the output's unit RGB range — NTSC-1953's Illuminant C
        white overshoots BT.709's red and blue channels by roughly 5-10%
        linear. Integer output clamps those channels (tinting the brightest
        highlights); float output carries the overshoot to whatever clips
        next. PQ and HLG output have headroom above SDR reference white and
        are unaffected. Enable ``chromatic_adaptation`` — or attenuate before
        conversion — when unclipped SDR highlights matter.

    :param float nominal_luminance:
        Physical luminance in cd/m² that linear 1.0 (SDR reference white)
        maps to for PQ and HLG output. Default ``100.0``.

    :param float contrast_in:
        BT.1886 user gain (legacy "contrast") for the input EOTF: the screen
        white luminance L\ :sub:`W` as a 0.0-1.0 fraction of reference white.
        Default ``1.0``.

    :param float brightness_in:
        BT.1886 black lift (legacy "brightness") for the input EOTF: Annex
        1's L\ :sub:`B`, or Appendix 1's ``b``, as a 0.0-1.0 fraction.
        Default ``0.0``.

    :param float contrast:
    :param float brightness:
        Output-side counterparts, valid only with the ``bt1886-annex-1``
        output transfer.

    Output frames are tagged with the matching ``_Matrix``, ``_Transfer``,
    ``_Primaries``, ``_ColorRange`` and ``_Range`` properties.
    ``_ChromaLocation`` is preserved when the output keeps the input's
    subsampling and removed otherwise (siting is moot at 4:4:4). The
    BT.1886 output transfer is signalled as BT.709 (``_Transfer=1``), or as
    the BT.2020 10/12-bit OETF tag when paired with BT.2020 primaries, since
    H.273 has no display-side code point for it.

Examples:

.. code-block:: python

    import vapoursynth as vs
    from vapoursynth import core

    clip = core.analog.decode_4fsc_video("/path/to/capture.tbc")

    # Straight to Rec. 709 SDTV-successor colorimetry:
    hd = core.analog.modernize_chromaticity(clip, output_preset="hdtv")

    # A LaserDisc mastered on a 1969-registered RCA tube, to BT.2100 PQ:
    pq = core.analog.modernize_chromaticity(
        clip, primaries_in_s="ecia-xxd", transfer_in_s="crt",
        output_preset="bt2100-pq")

    # Quantize to the 10-bit 4:2:0 an HDR10 deliverable calls for. This filter
    # only converts colorimetry, never depth, so the dither is yours to place.
    hdr10 = core.resize.Spline36(pq, format=vs.YUV420P10,
                                 dither_type="error_diffusion")

The remaining HDR10 ingredients — ST 2086 mastering-display metadata and
MaxCLL/MaxFALL — are static metadata carried alongside the picture rather than
in it, so they are set at encode time (x265's ``--master-display`` and
``--max-cll``), not by anything in the filter graph.


.. function:: core.analog.amplify_chroma(clip, gain \
        [, resample_filter_uv] \
        [, filter_param_a_uv] \
        [, filter_param_b_uv])

    Amplifies or attenuates the color-difference signals: the post-decode
    counterpart of ``decode_4fsc_video``'s ``chroma_gain``, which scales the
    demodulated color differences on their way out of the decoder. Because
    it works on a clip rather than a decode, a conventional capture loaded
    through a source plugin (BestSource and the like) is as valid an input as
    a fresh decode.

    Think of it as a saturation control, but saturation as the analog video
    domain defines it — a gain on E'Cb/E'Cr — rather than the saturation axis
    of an HSV or HLS model.

    Frames that already carry E'Y E'Cb E'Cr — 32-bit float Y'CbCr tagged
    ``_Matrix=4`` (NTSC-1953), ``_Matrix=5`` (BT.470 BG) or ``_Matrix=6``
    (SMPTE ST 170), as ``decode_4fsc_video`` emits — are scaled in place,
    leaving luma, chroma siting and every frame property alone. All three
    are the same luma/chroma split: the coefficients NTSC-1953 derived from
    its primaries at Illuminant C, code 4 at the precision they were first
    published to and codes 5 and 6 at the higher one later systems restated
    them to. Those later systems kept the coefficients even though their
    primaries and white had moved, which fits their own chromaticity no
    better — but it is how the signals were built, so it is what a gain on
    them means.

    Frames carrying those same color differences in another format — integer
    Y'CbCr of any depth — go through a 32-bit float intermediate of the same
    subsampling and back, both conversions running through the
    :external+vapoursynth:doc:`resize <functions/video/resize>` plugin. That
    trip names no matrix, so ``resize`` converts the samples and
    nothing else: no chroma is resampled, whichever analog matrix the frame
    carries, and even the 4:4:0 lattice comes back untouched.

    Frames on other axes take the same trip to a ``_Matrix=6`` intermediate
    instead, which is a real color-difference change and so does resample the
    chroma of a subsampled clip (see ``resample_filter_uv``). RGB always takes
    that path, having no color differences of its own. The choice is made per
    frame from the frame's own ``_Matrix``, which frames fail on if absent or
    ``unspecified``.

    On a frame whose matrix isn't an analog one, the round trip holds the
    *analog* luma constant, which is what the decoder's own gain does. Its
    own Y' therefore shifts a little as the color is scaled — a BT.709 frame
    taken to ``gain=0.0`` lands on the analog luma of its colors, not on its
    BT.709 one — while the chroma comes out scaled by exactly ``gain`` either
    way.

    Only analog-era colorimetry is accepted. A frame's ``_Primaries`` must be
    4 (NTSC-1953), 5 (EBU), 6 (SMPTE ST 170) or 7 (SMPTE ST 240), or absent
    or ``unspecified`` (2) — untagged captures are taken at their word.
    Anything newer is rejected: no signal was ever built by splitting luma
    from chroma with the analog coefficients on those primaries, and there is
    no telling what such a picture was originally broadcast in. Amplify
    before ``modernize_chromaticity`` (or another conversion) rather than
    after.

    :param vnode clip:
        Input clip: YUV or RGB, constant format. GRAY carries no chroma and
        is rejected.

    :param float gain:
        Multiplier applied to both color differences. Above ``1.0``
        amplifies and below ``1.0`` attenuates; ``0.0`` leaves a monochrome
        picture, and ``1.0`` returns the clip untouched, without the
        conversion round trip a unity gain could only lose precision to.
        Must be ``0.0`` or greater.

    :param str resample_filter_uv:
        Kernel for the chroma resampling a matrix change entails: ``point``,
        ``bilinear``, ``bicubic`` (default, as in ``resample_secam``),
        ``spline16``, ``spline36``, ``spline64``, or ``lanczos``. Only
        subsampled frames on non-analog axes use it — the matrix change
        happens at 4:4:4 and is resampled back to the source subsampling,
        sited against the frame's ``_ChromaLocation``. Frames already carrying
        analog color differences never reach it, in any format or
        subsampling.

        4:4:0 frames needing that matrix change are refused rather than
        resampled: SECAM from ``decode_4fsc_video`` carries a line-sequential
        Db/Dr lattice that plain resampling would blend, so realign it with
        ``resample_secam`` (or ``fill_secam_by_delay``) first. Analog-matrix
        4:4:0 is amplified normally, integer included, and needs no
        realignment — the gain is per sample.

    :param float filter_param_a_uv:
    :param float filter_param_b_uv:
        Kernel tuning for ``resample_filter_uv``, as in the ``resize``
        functions: the bicubic b/c coefficients, or the lanczos tap count
        (``filter_param_a_uv`` only).

    Output keeps the input's format, dimensions and frame properties.
    Integer output is rounded without dithering and clamps at the format's
    bounds, so amplifying an already saturated picture can flatten its most
    colorful areas; float clips nothing.

Examples:

.. code-block:: python

    import vapoursynth as vs
    from vapoursynth import core

    clip = core.analog.decode_4fsc_video("/path/to/capture.tbc")

    # A washed-out capture, given back some color:
    livelier = core.analog.amplify_chroma(clip, 1.2)

    # Equally at home on a conventional capture, whatever its colorimetry:
    vhs = core.bs.VideoSource("/path/to/vhs_capture.mkv")
    calmer = core.analog.amplify_chroma(vhs, 0.85)


Logging
-------

.. function:: core.analog.set_log_level(level)

    Sets the threshold for the decoder's diagnostic messages.

    Diagnostics that describe how a decode went but don't stop it — an
    accelerated neural-network backend falling back to CPU, a SECAM field ident
    that disagrees with the sidecar, a capture that isn't at a 4𝑓𝑠𝑐 sample rate
    — are emitted as VapourSynth log messages, so
    :py:meth:`core.add_log_handler <Core.add_log_handler>` receives them
    alongside every other filter's. Failures are reported the
    other way, as an error on the call that failed.

    ``level`` is one of ``debug``, ``info`` (the default), ``warning``,
    ``critical`` or ``off``. The setting is process-wide rather than per clip.

    .. note::

        Under ``vspipe``, or any other host that installs its own log
        handler, these messages go straight to that handler. Under a plain
        Python interpreter with no handler registered, VapourSynth's Python
        module forwards them to the standard library's :py:mod:`logging`
        instead, under the logger named ``"vapoursynth"``. Without a
        :py:func:`logging.basicConfig` call (or some other handler attached),
        only ``warning`` and above will actually reach stderr — ``debug`` and
        ``info`` are silently discarded. See the
        :external+python:doc:`logging HOWTO <howto/logging>` for how to
        configure it.


.. _active-window:

Active Window
-------------
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

    # ld-chroma-decoder's picture crop (the pre-libchromadec default)
    core.analog.decode_4fsc_video(src, first_active_sample=9,
                                  last_active_sample=768)  # NTSC, 760 wide
    core.analog.decode_4fsc_video(src, first_active_sample=8,
                                  last_active_sample=929)  # PAL, 922 wide

    # Analog active line (~52.66 us NTSC, ~52.0 us PAL); nominal, since line
    # blanking carries a few samples of tolerance
    core.analog.decode_4fsc_video(src, first_active_sample=9,
                                  last_active_sample=762)  # NTSC, 754 wide
    core.analog.decode_4fsc_video(src, first_active_sample=7,
                                  last_active_sample=928)  # PAL, 922 wide

    # Field 1's active picture only, on a 525-line source
    core.analog.decode_4fsc_video(src, first_active_line=21,
                                  last_active_line=263)

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


.. _decoder-options:

Decoder Options
---------------
The ``decoder`` parameter accepts the following values:

.. list-table::
    :header-rows: 1
    :widths: 20 15 65

    * - Decoder
      - Video System
      - Description
    * - ``ntsc1d``
      - NTSC
      - 1D comb filter
    * - ``ntsc2d``
      - NTSC
      - 2D comb filter (default for NTSC)
    * - ``ntsc3d``
      - NTSC
      - 3D adaptive comb filter
    * - ``ntsc3dnoadapt``
      - NTSC
      - 3D comb filter without adaptation
    * - ``pal2d``
      - PAL
      - 2D PALcolour filter (default for PAL)
    * - ``transform2d``
      - PAL
      - 2D Transform PAL frequency-domain filter
    * - ``transform3d``
      - PAL
      - 3D Transform PAL frequency-domain filter
    * - ``secam``
      - SECAM
      - Line-sequential FM chroma; outputs ``YUV440PS`` (4:4:0)
    * - ``nntransform3d``
      - NTSC
      - Neural 3D transform Y/C separation (needs a model)
    * - ``ldzeug2_color_cnn``
      - NTSC
      - Neural chroma separation/color CNN (needs a model)
    * - ``ldzeug2_luma_sep``
      - NTSC
      - Neural luma separation, field mode (needs a model)
    * - ``ldzeug2_luma_sep_frame``
      - NTSC
      - Neural luma separation, frame mode (needs a model)
    * - ``mono``
      - Any
      - Luma-only decode (no chroma)

If not specified, the decoder is auto-selected based on the video system. The
neural-network decoders are NTSC-only and require a model — see
``model_path``/``onnx_provider`` above, or the Python wrapper's
``model_version``.


.. _dropout-correction:

Dropout Correction
------------------
Setting ``dropout_correct=1`` replaces signal dropout regions identified in the
TBC metadata with data from nearby clean lines, using the algorithm libchromadec
carries over from `ld-decode-tools
<https://github.com/simoninns/ld-decode-tools>`_' ld-dropout-correct. Luma and chroma are sourced independently using FIR
frequency separation to find the closest match for each.

For multi-source correction, pass additional TBC captures of the same content
via ``dropout_composite_or_luma_extra_sources`` (and
``dropout_chroma_extra_sources`` for Y/C-separated formats). Sources are aligned
using VBI frame numbers when available (laserdisc CAV/CLV), falling back to
sequential frame alignment for sources without VBI data (e.g. VHS-decode
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

Dropout Annotation
------------------
Concealment *hides* dropouts; ``annotate_dropouts=1`` instead reports where they
are, in the ``AnalogDropoutSpans`` frame property, so another filter can act on
them. It is independent of ``dropout_correct``: annotate without correcting to
hand the damaged regions to an in-painter, or alongside it to see what was
concealed. Note that the regions describe *detected* damage — with
``dropout_correct=1`` they mark what has already been repaired, not what still
needs repairing.

The property is a flat integer array holding four values per region —
``y``, ``x_start``, ``x_end``, ``origin`` — in the decoded clip's own pixel
coordinates, with ``x`` half-open and regions sorted by ``y`` then ``x_start``.
One array rather than four parallel ones keeps its length off 1, which
VapourSynth's Python layer would hand back as a bare int instead of a list. The
property is present but empty on a frame with no dropouts, which is what
distinguishes an annotated clip from an unannotated one.

``origin`` is ``0`` for a region flagged upstream by ld-decode / vhs-decode and
stored in the sidecar, or ``8`` for one the decoder detected and concealed
itself — currently SECAM FM click concealment, which exists only because the
frame was decoded, and so has no counterpart in the sidecar.

``dropout_overcorrect=1`` widens the reported regions to the footprint
overcorrect-mode correction would touch, exactly as it widens what correction
overwrites. The widening happens against the full signal line before the active
crop is applied, so a dropout lying entirely in the blanking either side of the
picture can extend into the frame under ``dropout_overcorrect=1`` while being
absent altogether without it.


Metadata Sidecars
-----------------
Each source signal file must have a corresponding metadata sidecar file with
the same base name. For ``.tbc`` sources that is a ``.db`` (SQLite) or
``.json`` file, ``.db`` taking precedence when both are present; CVBS sources
carry a ``.meta`` sidecar. Both TBC forms are read directly and neither is
converted or written back — no ``.db`` is created alongside a ``.json`` source.
