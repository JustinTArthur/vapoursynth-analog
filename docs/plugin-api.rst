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
        [, reverse_fields=0] \
        [, chroma_gain=1.0] \
        [, chroma_phase=0.0] \
        [, chroma_nr=0.0] \
        [, luma_nr=0.0] \
        [, phase_compensation=0] \
        [, padding_multiple=8] \
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
    and must have a metadata sidecar file in JSON or SQLite format.

    For color decodes, returns a clip in ``YUV444PS`` format (32-bit float).
    For monochrome decodes, the clip is in ``GRAYS`` format.

    :param str composite_or_luma_source:
        Path to the composite or luma-only ``.tbc`` file.

    :param str chroma_or_pb_source:
        Path to a separate chroma ``.tbc`` file, for Y/C-separated sources such
        as S-Video or VHS color-under.

    :param str pr_source:
        Path to the Pr component ``.tbc`` file (component video, not yet
        supported).

    :param str decoder:
        Chroma decoder to use. See :ref:`decoder-options` below. When not
        specified, the decoder is chosen automatically based on the video system
        in the TBC metadata.

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
        Set to 1 to enable NTSC phase compensation. Default ``0``.

    :param int padding_multiple:
        Round output dimensions to a multiple of this value. Set to 0 to
        disable. Default ``8``.

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
    * - ``mono``
      - Any
      - Luma-only decode (no chroma)

If not specified, the decoder is auto-selected based on the video system in the
TBC metadata.


.. _dropout-correction:

Dropout Correction
^^^^^^^^^^^^^^^^^^
Setting ``dropout_correct=1`` replaces signal dropout regions identified in the
TBC metadata with data from nearby clean lines, based on the algorithm from
`ld-decode-tools <https://github.com/simoninns/ld-decode-tools>`_'
ld-dropout-correct. Luma and chroma are sourced independently using FIR
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
the same base name and a ``.db`` or ``.json`` extension. If the metadata is in
JSON format, a ``.db`` file will automatically be created in the same directory.
