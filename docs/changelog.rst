Changelog
=========

0.3.0a1
-------
- New ``nntransform3d`` decoder option for ``decode_4fsc_video``: an
  NTSC-only 3D adaptive comb that substitutes neural-network inference
  for the analytical FFT-based Y/C separation step. Two model versions
  (the original ``v1`` and the newer/faster ``v2``) ship in the wheel;
  pick one with ``model_version=`` or supply your own weights via
  ``model_path=``. PAL and PAL-M sources are rejected for this decoder
  because the model was trained on NTSC chroma encoding only. Network
  design and weights are by **asdfqazsnbb** (the nnTransform3D author);
  the surrounding ``ld-chroma-decoder`` integration is from
  `harrypm/tbc-tools <https://github.com/harrypm/tbc-tools>`_.
- New ``ldzeug2_luma_sep`` and ``ldzeug2_color_cnn`` decoders, NTSC-only,
  derived from **jsaowji**'s `ldzeug2
  <https://github.com/jsaowji/ldzeug2>`_ models (weights treated as
  public domain per the author). ``ldzeug2_luma_sep`` performs neural
  Y/C separation only (``model_version="field"|"frame"``) with
  downstream comb demodulation; ``ldzeug2_color_cnn``
  (``model_version="v1"|"v1_denoise"|"v2"``) performs joint NN
  separation and chroma demodulation in one pass, replacing the comb
  entirely. All five bundled weights are jsaowji's; see :doc:`/models`
  for the source-to-bundle filename mapping.
- New ``onnx_provider=`` argument on ``decode_4fsc_video`` to pin the
  ONNX Runtime execution provider used by neural decoders
  (``"auto"``, ``"cpu"``, ``"cuda"`` / ``"gpu"``, ``"migraphx"``,
  ``"tensorrt"`` / ``"trt"``, ``"coreml"``). On macOS, CoreML is
  requested automatically when ``onnx_provider`` is not specified.
- Switched the bundled signal-decoder submodule from
  ``simoninns/ld-decode-tools`` to the active
  ``harrypm/tbc-tools`` fork. Older JSON and SQLite metadata sidecar
  files remain readable; the SQLite schema is auto-migrated to the
  fork's expanded version on open.
- Added ``ONNX Runtime`` (1.x) as a build and runtime dependency. The
  CPU execution provider is bundled with binary wheels; install
  ``onnxruntime``/``onnxruntime-gpu`` separately to opt in to hardware
  acceleration.

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