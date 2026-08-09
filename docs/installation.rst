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

The PyPI wheels run the neural-network decoders on the CPU (Linux), on any
DirectX 12 GPU through DirectML — falling back to the CPU when no usable
device is present — (Windows), or on Apple's CoreML stack (macOS) — on Apple
silicon that includes the Neural Engine for the default ``nntransform3d``
model.

GPU-Accelerated Neural Decoding
-------------------------------
Wheels carrying a GPU execution provider are too large for PyPI and are
published separately, on the project's GitHub Releases or download site:

- **Linux:** CUDA/TensorRT (Nvidia), MIGraphX (AMD)
- **Windows:** CUDA/TensorRT (Nvidia)

DirectML needs no separate wheel: the PyPI Windows wheel bundles the
DML-capable Windows ML engine and drives any DX12 GPU on its own.

CUDA comes in two builds, ``0cuda12`` and ``0cuda13``, one per toolkit major.
Pick the one matching the CUDA runtime you have installed; if you have neither,
take ``0cuda13`` unless your card is older than Turing (GTX 16-series / RTX
20-series), since CUDA 13 dropped support for Maxwell, Pascal and Volta.

Install one over the plain wheel by URL:

.. code-block:: bash

    pip install https://.../vsanalog-<version>-0cuda13-py3-none-manylinux_2_28_x86_64.whl

They provide the same functions as the PyPI wheel; the extra provider is
selected with the ``onnx_provider`` argument (or left to ``auto``).

.. warning::

   The CUDA and MIGraphX wheels **require** the matching vendor runtime. They
   carry a device-resident FFT pipeline linked against it, so the plugin will
   not load at all when it is absent — this takes the ordinary PAL/NTSC/SECAM
   decoders down with it, not just the neural-network ones. The requirement is
   on the major version, not an exact release:

   - ``0cuda12`` wheels need a CUDA 12.x runtime (``libcudart.so.12``,
     ``libcufft.so.11``).
   - ``0cuda13`` wheels need a CUDA 13.x runtime (``libcudart.so.13``,
     ``libcufft.so.12`` — cuFFT carries its own version, one behind the
     toolkit's).
   - MIGraphX wheels need a ROCm 7.x installation (``libamdhip64.so.7``,
     ``libhipfft.so.0``).

   The PyPI wheels have no such requirement — on Windows that includes
   DirectML, which drives inference through ONNX Runtime alone and falls back
   to the CPU when no DX12 device is usable. If you want a wheel that degrades
   gracefully rather than one that needs a vendor runtime present, use the
   PyPI wheel.

For the CUDA wheels, the easiest way to supply the runtime is from PyPI, into
the same environment as the wheel — no ``LD_LIBRARY_PATH``/``PATH`` setup, and
the versions stay pinned with the rest of the environment. Each CUDA wheel
declares the matching packages as extras, so the install is one line:

.. code-block:: bash

    # CUDA runtime from PyPI, pinned to the wheel's toolkit major:
    pip install "vsanalog[cuda] @ https://.../vsanalog-<version>-0cuda13-py3-none-manylinux_2_28_x86_64.whl"

    # The same plus TensorRT (see the note below):
    pip install "vsanalog[tensorrt] @ https://.../vsanalog-<version>-0cuda13-py3-none-manylinux_2_28_x86_64.whl"

The extras differ per wheel — a ``0cuda12`` wheel pins the ``-cu12`` package
names, a ``0cuda13`` wheel the CUDA 13 ones — so there is nothing to spell by
hand; installing the wheel without an extra keeps today's behaviour of
supplying the runtime yourself.

The wheel resolves these at load time wherever they landed (the NVIDIA driver
itself still comes from the system — pip cannot install that). A system
CUDA/TensorRT installation keeps working exactly as before; when both are
present, the pip copies win. Set ``VSANALOG_GPU_RUNTIME=system`` to prefer the
system copies instead (``LD_LIBRARY_PATH``/``ldconfig``/``PATH`` first;
libraries the system does not provide can still come from the pip packages),
or ``VSANALOG_GPU_RUNTIME=off`` to disable the resolution step entirely. Each library's resolution (and anything that could not be found) is
reported on the VapourSynth log, visible through ``core.add_log_handler()``.

.. note::

   **TensorRT is not bundled**, and most users will not have it. Its absence is
   normal and costs nothing but the TensorRT speedup: the default
   ``onnx_provider="auto"`` tries TensorRT first, finds it unusable, and moves
   on to CUDA by itself. The decoded output is identical either way — only the
   provider changes.

   If you do install it, it must be **10.x** (``libnvinfer.so.10``), which is
   what the bundled ONNX Runtime loads. The ``[tensorrt]`` extra pins exactly
   that; installing TensorRT from PyPI by hand without a version constraint
   gets you 11.x, which these wheels cannot use — a wrong major is simply not
   picked up (the resolver looks for ``libnvinfer.so.10`` by name), so it
   degrades to the no-TensorRT behaviour above rather than erroring.

   NVIDIA publishes the TensorRT libraries on PyPI as stub packages that
   download the real wheel from ``pypi.nvidia.com`` while installing, so the
   extra needs no additional index — but it will not work in ``--only-binary``
   or hash-pinned installs.

   Pinning ``onnx_provider="tensorrt"`` without a usable TensorRT is the one
   case that costs you real speed: rather than fail, the plugin falls back to
   the CPU, which is far slower than the CUDA provider ``auto`` would have
   chosen. It logs a warning when it does — visible through
   ``core.add_log_handler()`` — but if in doubt, leave the provider on
   ``auto``.

.. note::

   The first GPU decode on a new machine is far slower than every one after it,
   and most of that cost is not ours. ONNX Runtime compiles its CUDA kernels for
   your GPU's architecture on first use — tens of megabytes of them, cached
   afterwards under ``~/.nv/ComputeCache`` — and TensorRT additionally builds an
   engine per model, cached alongside the other chromadec state. Measured on a
   T4, an nnTransform3D frame took around two minutes on the first TensorRT run
   and about a second once both caches were warm.

   Both caches are per-user and survive across runs, so this is a one-time cost
   per machine and GPU architecture, not per session. Wiping either one, or
   moving to a different GPU generation, pays it again.

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
