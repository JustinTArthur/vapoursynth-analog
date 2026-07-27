Installation
============

From PyPI
---------
The simplest way to install is via pip into a Python environment such as a
venv:

.. code-block:: bash

    pip install vsanalog

This installs both the native VapourSynth plugin and a Python module with
type-hinted wrappers. The plugin is automatically loaded when you use the
Python module. The wheel bundles its shared library dependencies and the
neural-network model weights, so no additional runtime libraries need to be
installed separately. Only VapourSynth itself is required.

The PyPI wheels run the neural-network decoders on the CPU (Linux, Windows) or
on Apple's CoreML stack (macOS).

GPU-Accelerated Neural Decoding
-------------------------------
Wheels carrying a GPU execution provider are too large for PyPI and are
published separately, on the project's GitHub Releases or download site:

- **Linux:** CUDA/TensorRT (Nvidia), MIGraphX (AMD)
- **Windows:** CUDA/TensorRT (Nvidia), DirectML (any DX12 GPU)

Install one over the plain wheel by URL:

.. code-block:: bash

    pip install https://.../vsanalog-<version>-0cuda-py3-none-manylinux_2_28_x86_64.whl

They provide the same functions as the PyPI wheel; the extra provider is
selected with the ``onnx_provider`` argument (or left to ``auto``).

.. warning::

   The CUDA and MIGraphX wheels **require** the matching vendor runtime. They
   carry a device-resident FFT pipeline linked against it, so the plugin will
   not load at all when it is absent — this takes the ordinary PAL/NTSC/SECAM
   decoders down with it, not just the neural-network ones. The requirement is
   on the major version, not an exact release:

   - CUDA wheels need a CUDA 12.x runtime (``libcudart.so.12``, ``libcufft.so.11``).
   - MIGraphX wheels need a ROCm 7.x installation (``libamdhip64.so.7``,
     ``libhipfft.so.0``).

   The DirectML wheel has no such requirement: DirectML drives inference
   through ONNX Runtime alone, so it loads anywhere and falls back to the CPU
   when no DX12 device is usable. If you want a wheel that degrades gracefully
   rather than one that needs a vendor runtime present, use the PyPI wheel.

Manual Plugin Installation
--------------------------
Alternatively, obtain or build the plugin for your operating system and place
``vsanalog.dll``, ``vsanalog.dylib``, or ``vsanalog.so`` into your VapourSynth
autoloading plugins directory.

Runtime Dependencies
~~~~~~~~~~~~~~~~~~~~
The plugin binaries attached to a release need only:

- **VapourSynth** (>= R55)

libchromadec, its SQLite, and FFTW are linked into the plugin, so nothing else
has to be installed. A plugin you build yourself follows whatever you linked
against — see :doc:`building` for a self-contained build.

Two things differ from the wheel:

- The neural-network decoders are built only on macOS (against the system
  CoreML frameworks). The Linux and Windows plugin binaries carry the
  analytical decoders — Comb, PalColour, Transform PAL, SECAM and mono — and
  omit the ONNX Runtime the neural ones would need. Install the wheel for
  those.
- No model weights are bundled. Neural decoding through the standalone plugin
  takes a ``model_path`` you supply.
