Bundled Neural-Network Models
=============================

The neural-network decoders in :func:`vsanalog.decode_4fsc_video` operate on
ONNX models that travel inside the wheel under ``vsanalog/models/``. The
model is selected indirectly: the ``decoder=`` argument picks which family
of model to use, and ``model_version=`` selects a variant within that
family. ``model_path=`` can override either with a user-supplied ``.onnx``
file using the same architecture.

Decoders and the model versions available to each
--------------------------------------------------

``nntransform3d``
~~~~~~~~~~~~~~~~~

NTSC-only. A neural network replaces the analytical 3D-FFT Y/C separation
step inside the existing 3D adaptive comb. Originally released by the
nnTransform3D author under the handle **asdfqazsnbb**; further integrated
into ``ld-chroma-decoder`` by harrypm's `tbc-tools
<https://github.com/harrypm/tbc-tools>`_ fork, which vapoursynth-analog
bundles. The downstream comb stages (I/Q split, NR, ``transformIQ``) are
unchanged, so the usual chroma-tuning knobs (``chroma_gain``,
``chroma_phase``, ``chroma_nr``, ``luma_nr``, ``phase_compensation``) still
apply.

.. list-table::
    :header-rows: 1
    :widths: 15 85

    * - ``model_version``
      - Description
    * - ``"v1"``
      - Original CPU release of the chroma gain-mask network. Matches the
        weights the nnTransform3D author shipped as
        ``nnTransform3D_win_cpu/chroma_net.onnx``.
    * - ``"v2"`` *(default)*
      - Newer, faster weights shipped with the author's v2 GPU release
        (``nnTransform3D_v2.0_win_gpu/chroma_net.onnx``). Same network
        architecture as v1.

``ldzeug2_color_cnn``
~~~~~~~~~~~~~~~~~~~~~

NTSC-only. A neural network performs joint Y/C separation **and** chroma
demodulation in one inference pass, bypassing the comb pipeline entirely.
Derived from **jsaowji**'s `ldzeug2 <https://github.com/jsaowji/ldzeug2>`_
project. The plugin synthesizes the I/Q-carrier planes analytically from
sample position and ``fieldPhase``, then feeds the network a 3-channel
``[CVBS, I-carrier, Q-carrier]`` stack and applies ``uv_from_iq`` rotation
to the network's ``[Y, I, Q]`` output to derive Y/U/V.

All bundled ``ldzeug2_color_cnn`` weights are per-field; the decoder runs
one inference per field and weaves the outputs into the final frame. If a
weaved-frame ``ColorCNN`` weights file becomes available in the future, it
will get its own ``decoder=`` name (e.g. ``ldzeug2_color_cnn_frame``)
because field and frame are different input pipelines, not a weights swap.

.. list-table::
    :header-rows: 1
    :widths: 28 72

    * - ``model_version``
      - Description
    * - ``"1031640"``
      - jsaowji's original ``ColorCNN`` weights (``color_cnn_1031640.onnx``).
    * - ``"denoise_613928_ft22k"``
      - The same ``ColorCNN`` architecture, fine-tuned by jsaowji with a
        denoising emphasis (``color_cnn_denoise_613928_ft22k.onnx``).
    * - ``"v2_alot"`` *(default)*
      - jsaowji's newer ``ColorCNNV2`` architecture
        (``color_cnn_v2_alot.onnx``) — single-branch 64-feature × 16-conv
        network with two learnable I/Q-carrier-mixing parameters at the
        midpoint. Greater capacity than the original ``ColorCNN``.

``ldzeug2_luma_sep`` / ``ldzeug2_luma_sep_frame``
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

NTSC-only. A neural network extracts the luma plane from the CVBS signal;
chroma is then recovered analytically as ``CVBS − Y``, run through a
quadrature I/Q demodulator (mirroring ``ld-chroma-decoder``'s ``splitIQ``),
optionally passed through a 17-tap low-pass FIR (the ``c_colorlp_b``
coefficients from tbc-tools), and finally rotated to U/V via
``uv_from_iq``. Derived from **jsaowji**'s `ldzeug2
<https://github.com/jsaowji/ldzeug2>`_.

Field and weaved-frame are different input pipelines (one inference per
field vs. one per frame), so they're exposed as separate decoder names
rather than ``model_version`` variants: pick ``ldzeug2_luma_sep`` for the
per-field network and ``ldzeug2_luma_sep_frame`` for the weaved-frame one.
The ``model_chroma_bandpass`` kwarg (default: enabled) toggles the FIR
step for either, mirroring jsaowji's ``comb_split_already(..., color_bp=True)``.

.. list-table::
    :header-rows: 1
    :widths: 26 38 36

    * - ``decoder``
      - ``model_version``
      - Description
    * - ``"ldzeug2_luma_sep"``
      - ``"2dgray_fields"`` *(default)*
      - Per-field ``compact`` CNN trained on individual fields
        (``luma_sep_2dgray_fields.onnx``). Better motion handling at the
        cost of less stationary detail.
    * - ``"ldzeug2_luma_sep_frame"``
      - ``"2d_frame_gray_gray_run2_latest"`` *(default)*
      - Same architecture trained on weaved frames
        (``luma_sep_2d_frame_gray_gray_run2_latest.onnx``). Retains more
        stationary detail; less robust on motion.

Custom weights
--------------

To use a model file not bundled with the wheel, pass its path as
``model_path``. The file must have the same input and output tensor shapes
as the corresponding bundled model — i.e. it must be a drop-in replacement
for the architecture of the selected ``decoder``.

.. code-block:: python

    vsanalog.decode_4fsc_video(
        "capture.tbc",
        decoder="ldzeug2_color_cnn",
        model_path="/path/to/my_color_cnn.onnx",
    )

Credits
-------

The bundled NN weights are not original work of this project:

* ``nntransform3d`` models: by **asdfqazsnbb**.
  Distributed as part of public nnTransform3D release artifacts and through
  the `harrypm/tbc-tools <https://github.com/harrypm/tbc-tools>`_ fork of
  ``ld-decode-tools``.
* ``ldzeug2_*`` models: by **jsaowji**, distributed with `ldzeug2
  <https://github.com/jsaowji/ldzeug2>`_.

vapoursynth-analog provides the integration scaffolding (input tensor
preparation, ONNX Runtime session management, output reassembly) and ships
the weights as a convenience.