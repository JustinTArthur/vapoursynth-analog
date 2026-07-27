VapourSynth Plugin API
======================
The low-level VapourSynth plugin is registered under the ``analog`` namespace.
It can be called directly from VapourSynth scripts without the Python wrapper.

``analog.decode_4fsc_video``
----------------------------

.. function:: core.analog.decode_4fsc_video(\
        composite_or_luma_source \
        [, chroma_or_pb_source] \
        [, pr_source] \
        [, decoder] \
        [, color_family] \
        [, chroma_filter] \
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
        [, fpsnum] \
        [, fpsden=1])

    Decodes 4𝑓𝑠𝑐 (four times subcarrier frequency) sampled analog video signals
    to a digital video clip. The signal data must be orthogonal video system
    lines with well-formed blanking and syncing structure at a stable time
    base, such as those produced by
    `ld-decode <https://github.com/happycube/ld-decode>`_ and
    `vhs-decode <https://github.com/oyvindln/vhs-decode>`_. These files
    normally have a ``.tbc`` extension indicating they are time-base-corrected
    and must have a metadata sidecar file in JSON or SQLite format. The newer
    CVBS format is also read, detected by extension: ``.composite``, or
    ``.y``/``.c`` for separated luma/chroma. RAW CVBS encodings are rejected.

    Returns a 32-bit float clip: ``YUV444PS`` (default), ``RGBS``, ``GRAYS``, or
    ``YUV440PS`` for SECAM, per ``color_family``.

    Note: this is the low-level interface. Neural-network ``decoder`` values
    need an explicit ``model_path``; the :doc:`Python wrapper <python-api>`
    resolves bundled models by ``model_version`` for you.

    :param str composite_or_luma_source:
        Path to the composite or luma-only capture (``.tbc``/``.composite``).

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

    :param str chroma_filter:
        Chroma bandpass/notch: ``"compat"``, ``"equiband_wide"``,
        ``"equiband"``, ``"color_under"``, ``"wideband_i_ssb"``,
        ``"equiband_vsb"``.

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

    :param str[] dropout_chroma_extra_sources:
        Additional chroma ``.tbc`` files for multi-source dropout correction
        (for color-under formats).

    :param int fpsnum:
        Override frame rate numerator. When not specified, frame rate is
        auto-detected from metadata.

    :param int fpsden:
        Override frame rate denominator (used with ``fpsnum``). Default ``1``.


Usage
^^^^^
.. code-block:: python

    import vapoursynth as vs
    from vapoursynth import core

    # Basic composite decode:
    clip = core.analog.decode_4fsc_video("/path/to/capture.tbc")

    # Y/C-separated decode:
    clip = core.analog.decode_4fsc_video("luma.tbc", "chroma.tbc")


.. _active-window:

Active Window
^^^^^^^^^^^^^
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
downstream with ``std.AddBorders`` if a codec needs particular dimensions.


.. _decoder-options:

Decoder Options
^^^^^^^^^^^^^^^
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
^^^^^^^^^^^^^^^^^^
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


Metadata Sidecars
^^^^^^^^^^^^^^^^^
Each source signal file must have a corresponding metadata sidecar file with
the same base name. For ``.tbc`` sources that is a ``.db`` (SQLite) or
``.json`` file, ``.db`` taking precedence when both are present; CVBS sources
carry a ``.meta`` sidecar. Both TBC forms are read directly and neither is
converted or written back — no ``.db`` is created alongside a ``.json`` source.


``analog.set_log_level``
------------------------

.. function:: core.analog.set_log_level(level)

    Sets the threshold for the decoder's diagnostic messages.

    Diagnostics that describe how a decode went but don't stop it — an
    accelerated neural-network backend falling back to CPU, a SECAM field ident
    that disagrees with the sidecar, a capture that isn't at a 4𝑓𝑠𝑐 sample rate
    — are emitted as VapourSynth log messages, so ``core.add_log_handler``
    receives them alongside every other filter's. Failures are reported the
    other way, as an error on the call that failed.

    ``level`` is one of ``debug``, ``info`` (the default), ``warning``,
    ``critical`` or ``off``. The setting is process-wide rather than per clip.
