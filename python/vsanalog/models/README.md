# Neural-network models

This directory is **intentionally empty in the repository**. The NN model weights
have separate licensing and are large, so they are not committed.

At build time, CI populates this directory from
[`tools/models.lock`](../../../tools/models.lock):

- `tools/fetch_models.py` downloads each `.onnx` and verifies its SHA-256.
- On macOS, `tools/convert_models_macos.py` converts them to native CoreML
  `.mlpackage` bundles (libchromadec uses the native CoreML backend there, with
  no ONNX Runtime) and removes the `.onnx`.

The wheel install step (`python/vsanalog/meson.build`) bundles whatever models
are present here. See the models page for weights and checksums:
<https://justinarthur.com/av/analog-decoding/models/>

Model authorship and credits are documented in the project docs.
