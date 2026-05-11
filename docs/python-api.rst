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
        model_version=None, \
        model_path=None, \
        onnx_provider=None, \
        reverse_fields=False, \
        chroma_gain=1.0, \
        chroma_phase=0.0, \
        chroma_nr=0.0, \
        luma_nr=0.0, \
        phase_compensation=False, \
        padding_multiple=8, \
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
    and must have a metadata sidecar file in JSON or SQLite format.

    For color decodes, returns a clip in ``YUV444PS`` format (32-bit float).
    For monochrome decodes, the clip is in ``GRAYS`` format.

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
        Chroma decoder to use. One of ``"ntsc1d"``, ``"ntsc2d"``,
        ``"ntsc3d"``, ``"ntsc3dnoadapt"``, ``"nntransform3d"``,
        ``"ldzeug2_color_cnn"``, ``"ldzeug2_luma_sep"``, ``"pal2d"``,
        ``"transform2d"``, ``"transform3d"``, or ``"mono"``. When *None*, the
        decoder is chosen automatically based on the video system in the TBC
        metadata.

        Neural-network decoders (those that load ONNX model weights at
        runtime) are NTSC-only and reject PAL and PAL-M sources. They take an
        additional ``model_version`` or ``model_path`` argument; see below.
    :type decoder: :py:class:`str` | None

    :param model_version:
        Selects which bundled ONNX model to use when *decoder* is a
        neural-network decoder.

        * ``decoder="nntransform3d"``: ``"v1"`` or ``"v2"`` (default).
          Matches the nnTransform3D author's original v1/v2 designations.
        * ``decoder="ldzeug2_color_cnn"``: ``"v1"``, ``"v1_denoise"``, or
          ``"v2"`` (default).
        * ``decoder="ldzeug2_luma_sep"``: ``"field"`` (default) or
          ``"frame"``.

        Ignored for non-neural decoders.
    :type model_version: :py:class:`str` | None

    :param model_path:
        Filesystem path to a custom ONNX model file. When supplied this
        overrides *model_version*. Useful for testing alternative weights
        with the same architecture as the selected decoder. Ignored for
        non-neural decoders.
    :type model_path: :py:class:`str` | :py:class:`~pathlib.Path` | None

    :param onnx_provider:
        Optional override for the ONNX Runtime execution provider used by
        neural-network decoders. Recognized values: ``"auto"`` (default —
        CoreML on macOS, CPU on Linux/Windows), ``"cpu"``,
        ``"cuda"`` / ``"gpu"``, ``"migraphx"``, ``"tensorrt"`` / ``"trt"``,
        ``"coreml"``. The matching provider library (e.g.
        ``libonnxruntime_providers_cuda.so``) must be installed next to the
        bundled ``libonnxruntime`` for the request to succeed.

        For ``decoder="nntransform3d"`` only ``auto``/``cpu``/``cuda``/
        ``gpu``/``coreml`` are currently wired; passing ``migraphx``,
        ``tensorrt``, or ``trt`` raises :class:`ValueError`. The
        ``ldzeug2_*`` decoders accept the full provider set.
    :type onnx_provider: :py:class:`str` | None

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
        Enable NTSC phase compensation.

    :param int padding_multiple:
        Round output dimensions to a multiple of this value. Set to ``0`` to
        disable padding.

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

Neural-Network Decode (NTSC only)
^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^

The ``nntransform3d`` decoder substitutes the analytical 3D-FFT Y/C
separation step inside the 3D adaptive comb with neural-network inference
based on
`nnTransform3D <https://github.com/oyvindln/vhs-decode/discussions>`_.
Source restrictions: NTSC composite at 4𝑓𝑠𝑐. PAL and PAL-M are rejected
because the model was trained on NTSC chroma encoding only.

.. code-block:: python

    # Default (v2 weights — newer/faster):
    clip = decode_4fsc_video("ntsc_capture.tbc", decoder="nntransform3d")

    # Pin to the original v1 weights:
    clip = decode_4fsc_video(
        "ntsc_capture.tbc",
        decoder="nntransform3d",
        model_version="v1",
    )

    # Use a custom ONNX model with the same architecture
    # (input ``[B,2,4,16,16]`` → output ``[B,1,4,16,16]``):
    clip = decode_4fsc_video(
        "ntsc_capture.tbc",
        decoder="nntransform3d",
        model_path="/path/to/my_chroma_net.onnx",
    )

The other comb-related arguments (``chroma_gain``, ``chroma_phase``,
``chroma_nr``, ``luma_nr``, ``phase_compensation``) still apply to the
downstream pipeline.

ldzeug2 decoders
^^^^^^^^^^^^^^^^

Two additional NN decoders adapted from jsaowji's
`ldzeug2 <https://github.com/jsaowji/ldzeug2>`_ project:

* ``decoder="ldzeug2_color_cnn"`` — joint Y/C separator and chroma
  demodulator. One inference per field; bypasses the comb pipeline.
  Bundled weights: ``model_version="v1"``, ``"v1_denoise"``, ``"v2"``
  (default).
* ``decoder="ldzeug2_luma_sep"`` — luma-only NN extractor.
  ``model_version="field"`` (default) or ``"frame"``. In the current
  prerelease this decoder emits luma only and writes neutral chroma; the
  downstream analytical chroma demodulator is a planned follow-up.

Both reject PAL and PAL-M sources for the same reason as ``nntransform3d``.

Hardware acceleration
^^^^^^^^^^^^^^^^^^^^^

By default neural decoders use the CoreML provider on macOS and the CPU
provider on Linux/Windows. Pass ``onnx_provider=`` to select a different
ONNX Runtime execution provider; the matching provider library must be
installed next to the bundled ``libonnxruntime``.

.. code-block:: python

    # Use NVIDIA CUDA on Linux/Windows (requires CUDA Toolkit + cuDNN
    # installed system-wide and libonnxruntime_providers_cuda.so/.dll
    # placed next to the bundled libonnxruntime):
    clip = decode_4fsc_video(
        "ntsc_capture.tbc",
        decoder="ldzeug2_color_cnn",
        onnx_provider="cuda",
    )

    # Force CPU even on macOS (useful for benchmarking — for
    # nnTransform3D specifically, CPU can be faster than CoreML because
    # of per-block invocation overhead):
    clip = decode_4fsc_video(
        "ntsc_capture.tbc",
        decoder="nntransform3d",
        onnx_provider="cpu",
    )

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


Utility: ``requires_plugin``
----------------------------
.. autofunction:: vsanalog.requires_plugin