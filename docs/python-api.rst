Python API
==========
The ``vsanalog`` Python package provides a high-level, type-hinted interface to
the vsanalog VapourSynth plugin. It handles plugin loading automatically and
accepts Python-native types like :py:class:`~pathlib.Path` and :py:class:`bool`.

``vsanalog.decode_4fsc_video``
------------------------------

.. py:function:: vsanalog.decode_4fsc_video(\
        composite_or_luma_source, \
        chroma_or_pb_source=None, \
        pr_source=None, \
        *, \
        decoder=None, \
        color_family=None, \
        chroma_filter=None, \
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
        dropout_composite_or_luma_extra_sources=None, \
        dropout_chroma_extra_sources=None, \
        fpsnum=None, \
        fpsden=1)

    Decode 4𝑓𝑠𝑐 (four times subcarrier frequency) sampled analog video
    signals to a digital video clip. The signal data must be orthogonal video
    system lines with well-formed blanking and syncing structure at a stable
    time base, such as those produced by
    `ld-decode <https://github.com/happycube/ld-decode>`_ and
    `vhs-decode <https://github.com/oyvindln/vhs-decode>`_. These files
    normally have a ``.tbc`` extension indicating they are time-base-corrected
    and must have a metadata sidecar file in JSON or SQLite format. The newer
    CVBS format is also read, detected by extension: ``.composite`` for
    composite, or ``.y``/``.c`` for separated luma/chroma (with a ``.meta``
    sidecar). RAW (unscaled-ADC) CVBS encodings are not supported.

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

    :param chroma_filter:
        Chroma bandpass/notch selection: ``"compat"``, ``"equiband_wide"``,
        ``"equiband"``, ``"color_under"``, ``"wideband_i_ssb"``, or
        ``"equiband_vsb"``. When *None*, the decoder default is used.
    :type chroma_filter: :py:class:`str` | None

    :param color_difference_precision:
        Color-difference matrix precision: ``"classic"`` or ``"modern"``.
    :type color_difference_precision: :py:class:`str` | None

    :param broadcast_scaling_precision:
        Broadcast-safe scaling precision: ``"classic"``, ``"modern"`` or
        ``"scientific"``.
    :type broadcast_scaling_precision: :py:class:`str` | None

    :param model_version:
        Bundled model to use for a neural-network *decoder* (defaults per
        decoder). Ignored when *model_path* is given.
    :type model_version: :py:class:`str` | None

    :param model_path:
        Path to custom model weights (``.onnx``, or a CoreML ``.mlpackage`` on
        macOS) for a neural-network *decoder*.
    :type model_path: :py:class:`str` | :py:class:`~pathlib.Path` | None

    :param model_input_scale:
        Override the model input magnitude divisor (``nntransform3d`` only).
    :type model_input_scale: :py:class:`float` | None

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

    :param dropout_composite_or_luma_extra_sources:
        Additional composite or luma ``.tbc`` files for multi-source dropout
        correction.
    :type dropout_composite_or_luma_extra_sources: :py:class:`~collections.abc.Sequence`\[:py:class:`str` | :py:class:`~pathlib.Path`] | None

    :param dropout_chroma_extra_sources:
        Additional chroma ``.tbc`` files for multi-source dropout correction
        (for color-under formats).
    :type dropout_chroma_extra_sources: :py:class:`~collections.abc.Sequence`\[:py:class:`str` | :py:class:`~pathlib.Path`] | None

    :param fpsnum:
        Override frame-rate numerator. When not specified, frame rate is
        auto-detected from metadata.
    :type fpsnum: :py:class:`int` | None

    :param int fpsden:
        Override frame-rate denominator (used with *fpsnum*).

    :rtype: :py:class:`~vapoursynth.VideoNode`

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


SECAM Chroma: ``resample_secam`` / ``fill_secam_by_delay``
----------------------------------------------------------
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


Utility: ``requires_plugin``
----------------------------
.. autofunction:: vsanalog.requires_plugin

Utility: ``set_log_level``
--------------------------
.. autofunction:: vsanalog.set_log_level
