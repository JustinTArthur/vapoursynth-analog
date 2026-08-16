Building from Source
====================
This guide covers two separate build paths:

- Building the native VapourSynth plugin on its own, to be dropped into a
  VapourSynth plugins directory.
- Building Python distributions (sdist and wheel) that bundle the plugin
  together with the ``vsanalog`` Python wrapper for installation via ``pip``.

Both pull in `libchromadec
<https://github.com/JustinTArthur/libchromadec>`_ — the decoding library — as
a Meson `git wrap <https://mesonbuild.com/Wrap-dependency-system-manual.html>`_
declared in ``subprojects/chromadec.wrap``, and link it statically. Meson
fetches it (and libchromadec's own bundled SQLite) on first configure, so the
first build of a fresh checkout needs network access.

Building the VapourSynth Plugin
-------------------------------
The steps in this section produce a standalone plugin binary
(``vsanalog.dll`` / ``.dylib`` / ``.so``) for manual installation. If you only
intend to build a Python wheel for redistribution, skip ahead to
`Building Python Distributions`_ — those instructions invoke this build
internally.

Build Dependencies
~~~~~~~~~~~~~~~~~~
- `Meson <https://mesonbuild.com/>`_ build system (not 1.11.0, which
  meson-python rejects) and `Ninja <https://ninja-build.org/>`_
- C++20 compiler (GCC 10+, Clang 12+, or MSVC 2019+)
- ``git``, for the libchromadec wrap
- `VapourSynth <https://www.vapoursynth.com/>`_ (>= R55) development headers

Neither FFTW3 nor SQLite is a build dependency. Both are built from source and
linked statically into the plugin, via ``force_fallback_for=sqlite3,fftw3`` in
``meson.build``: SQLite from the trimmed amalgamation libchromadec ships, and
`FFTW3 <http://www.fftw.org/>`_ from ``subprojects/fftw3.wrap``, which downloads
the upstream tarball and builds it with the Meson port in
``subprojects/packagefiles/fftw3/``. Qt is no longer used at all.

Building FFTW rather than installing it is partly a performance decision and
partly a correctness one. The packaged builds ship the wrong codelets for several
targets: Homebrew and Fedora both produce a scalar-only aarch64 FFTW, and
Fedora's x86_64 build has no AVX2/FMA. The 3D decoders (``transform3d``,
``nntransform3d``) spend most of their time in FFTW, and the payoff tracks vector
width — roughly 17% on the arm64 targets, which go from scalar to 128-bit NEON,
but only about 1% on x86_64, where AVX to AVX2 is no change in width. The 2D path
is far less sensitive either way. On x86_64 the wrap therefore earns its place
through uniformity and through building at all where no FFTW is installed, rather
than through speed. The port enables SSE2/AVX/AVX2
on x86 (FFTW dispatches between them at plan time, so this stays safe on older
CPUs) and NEON on aarch64. SVE is deliberately left off: FFTW selects ARM
codelets at compile time with no runtime dispatch, so an SVE build would not run
on hardware without it.

To link system copies instead — the usual choice when packaging for a distro —
pass ``-Dforce_fallback_for=``, optionally with ``--wrap-mode=nodownload``::

    meson setup build -Dforce_fallback_for= --wrap-mode=nodownload

That needs FFTW3 and SQLite3 development packages installed — both really are
required. libchromadec declares FFTW optional and reports it as
``FFTW3 (Transform-PAL)`` in its feature summary, but that gate only selects the
``CHD_WITH_FFTW`` feature flag: ``palcolour.h`` includes ``transform_pal.h``, and
so ``<fftw3.h>``, unconditionally, and every build pulls that in through
``decoders/registry.cpp``. With no FFTW headers present the build fails outright
rather than dropping the Transform PAL decoders.

The neural-network decoders need a backend, selected by the ``ep``
(execution provider) option — see `Neural-Network Backends`_ below. On macOS
they are always built against the system CoreML frameworks; elsewhere they need
`ONNX Runtime <https://onnxruntime.ai/>`_ and are omitted with ``-Dep=none``.

Installing Build Dependencies
^^^^^^^^^^^^^^^^^^^^^^^^^^^^^

**macOS (Homebrew):**

.. code-block:: bash

    brew install meson ninja vapoursynth

**Ubuntu/Debian:**

.. code-block:: bash

    sudo apt install meson ninja-build build-essential git vapoursynth-dev

**Fedora:**

.. code-block:: bash

    sudo dnf install meson ninja-build gcc-c++ git vapoursynth-devel

**Arch Linux:**

.. code-block:: bash

    sudo pacman -S meson ninja base-devel git vapoursynth

Compiling the Plugin
~~~~~~~~~~~~~~~~~~~~

.. code-block:: bash

    git clone https://github.com/JustinTArthur/vapoursynth-analog.git
    cd vapoursynth-analog
    meson setup build
    meson compile -C build

There are no git submodules to initialize; ``meson setup`` fetches
libchromadec into ``subprojects/`` itself.

The plugin will be built as:

- ``build/vsanalog.dylib`` (macOS)
- ``build/vsanalog.so`` (Linux)
- ``build/vsanalog.dll`` (Windows)

Neural-Network Backends
^^^^^^^^^^^^^^^^^^^^^^^
``-Dep=<provider>`` picks the execution provider libchromadec's neural-network
decoders are built for: ``cpu`` (default), ``cuda``, ``migraphx``,
``directml``, ``coreml``, or ``none``.

- **macOS** ignores the value and always builds the native CoreML backend, with
  no ONNX Runtime involved.
- **Linux and Windows** need an ONNX Runtime to build anything but ``none``.
  Point the build at an unpacked `ONNX Runtime release
  <https://github.com/microsoft/onnxruntime/releases>`_::

    meson setup build -Dep=cpu -Dchromadec:onnxruntime_root=/opt/ort

- ``-Dep=none`` drops the neural-network decoders entirely. The analytical
  decoders (Comb, PalColour, Transform PAL, SECAM, mono) are always built.

Model weights are not part of the build. The wheels bundle them; a plugin built
this way takes an explicit ``model_path`` instead. ``tools/fetch_models.py``
downloads and checksum-verifies the same weights from ``tools/models.lock``.

On macOS the weights then go through ``tools/convert_models_macos.py``, which
drives libchromadec's ``scripts/convert_coreml.py`` to produce the
``.mlpackage`` bundles the native CoreML backend loads. It converts at fp32
except the ``nntransform3d`` ``v2`` model, which it converts at fp16 on Apple
silicon so the Apple Neural Engine — an fp16-only device — can take it. Pass
``--fp16 never`` for an all-fp32 set, or ``--fp16 always`` to convert fp16 on
an Intel Mac too (where it lands on the GPU and measures slightly slower than
fp32). No other bundled model survives fp16: the ``v1`` chroma_net series
overflows its range and the ldzeug2 models break on index math.

The CUDA and Windows wheels additionally run ``tools/convert_models_fp16.py``,
which writes an fp16 copy of the same fp16-safe model beside the fp32 one
(``chroma_net-v2-...-fp16.onnx``, via ``onnxruntime.transformers.float16``
with the graph's inputs and outputs pinned to fp32). The CUDA and DirectML
execution providers have no engine-level fp16 mode of their own — TensorRT
does, and keeps using the fp32 file — so this sibling is what an explicit
``onnx_provider="cuda"`` or ``"directml"`` loads at the default ``"fp16"``
precision. It needs only the pip ``onnxruntime`` and ``onnx`` packages for
its converter.

Self-Contained Plugin Builds
^^^^^^^^^^^^^^^^^^^^^^^^^^^^
A default build is already self-contained apart from ONNX Runtime: FFTW and
SQLite are static subprojects absorbed into the plugin. ``-Dprefer_static=true``
takes the static archive of anything else that offers one, which is what the
release tarballs are built with:

.. code-block:: bash

    meson setup build -Dep=none -Dprefer_static=true
    meson compile -C build

The result depends on nothing outside the C++ runtime, VapourSynth, and — on
macOS — the system CoreML frameworks.

Installing the Plugin
~~~~~~~~~~~~~~~~~~~~~
These steps apply to the standalone plugin built above. If you are installing
via a wheel produced by the Python distribution build, ``pip`` handles
placement automatically and you can skip this section.

Copy the built plugin to your VapourSynth plugins directory.

**macOS:**

.. code-block:: bash

    cp build/vsanalog.dylib ~/Library/Application\ Support/VapourSynth/plugins64/

**Linux:**

.. code-block:: bash

    cp build/vsanalog.so ~/.local/lib/vapoursynth/

Or load it explicitly in your VapourSynth script:

.. code-block:: python

    core.std.LoadPlugin("/path/to/vsanalog.dylib")

Building Python Distributions
-----------------------------
``vsanalog`` uses `meson-python <https://meson-python.readthedocs.io/>`_ as
its build backend, so the standard `build <https://build.pypa.io/>`_ front end
produces both sdists and wheels.

Install the front end once:

.. code-block:: bash

    pip install build

Source Distribution (sdist)
~~~~~~~~~~~~~~~~~~~~~~~~~~~
An sdist bundles this project's own source and is platform independent:

.. code-block:: bash

    python -m build --sdist

It carries the libchromadec and FFTW wraps rather than their sources, so
building from it fetches both over the network. It carries no model weights
either: install from an sdist and the neural-network decoders need models you
supply yourself.

The resulting ``dist/vsanalog-<version>.tar.gz`` can be uploaded to PyPI or
used as input to a wheel build on another machine.

Wheel
~~~~~
Building a wheel compiles the native plugin, so the same build dependencies
listed above must be available. Whatever shared libraries the plugin ended up
linking (in practice only ONNX Runtime — FFTW and SQLite are static
subprojects) are not present on end-user machines, so the platform-appropriate
repair tool vendors them into the wheel.

After repair, the wheels are re-tagged as ``py3-none`` because the plugin does
not embed CPython's ABI:

.. code-block:: bash

    python -m wheel tags --python-tag py3 --abi-tag none <wheel> --remove

**macOS:**

.. code-block:: bash

    pip install build delocate wheel
    python -m build --wheel -o dist/ -Csetup-args="-Dprefer_static=true"
    delocate-wheel -w wheelhouse/ dist/*.whl
    python -m wheel tags --python-tag py3 --abi-tag none wheelhouse/*.whl --remove

``PKG_CONFIG_PATH`` should resolve ``vapoursynth.pc`` —
``/opt/homebrew/lib/pkgconfig`` on Apple silicon.

**Linux (manylinux):**

Linux wheels are built inside a ``quay.io/pypa/manylinux_2_28_x86_64`` (or
``manylinux_2_34_aarch64``) container so that the resulting binaries are
compatible with older glibc versions. Inside the container, install the build
dependencies (``gcc-c++``, ``git``, an unpacked ONNX Runtime, and the
VapourSynth headers), then:

.. code-block:: bash

    pip install build auditwheel wheel meson ninja meson-python
    python -m build --wheel --no-isolation -o /tmp/dist/ \
        -Csetup-args="-Dep=cpu" \
        -Csetup-args="-Dprefer_static=true" \
        -Csetup-args="-Dchromadec:onnxruntime_root=/opt/ort"
    auditwheel repair -w wheelhouse/ /tmp/dist/*.whl
    python -m wheel tags --python-tag py3 --abi-tag none wheelhouse/*.whl --remove

See ``.github/workflows/build.yml`` for the full container setup used in CI.

**Windows:**

Windows wheels need an MSVC developer environment and an unpacked ONNX
Runtime. ``delvewheel`` vendors the DLLs from the ONNX Runtime directory:

.. code-block:: powershell

    pip install build delvewheel wheel
    python -m build --wheel -o dist/ `
        -Csetup-args="-Dep=cpu" `
        -Csetup-args="-Dchromadec:onnxruntime_root=$env:ORT_ROOT"
    delvewheel repair --analyze-existing `
        --namespace-pkg "vapoursynth;vapoursynth.plugins;vapoursynth.plugins.vsanalog" `
        --add-path "$env:ORT_ROOT/lib" `
        dist/*.whl -w repaired/
    python tools/colocate_plugin_libs.py (Get-ChildItem repaired/*.whl) wheelhouse/
    python -m wheel tags --python-tag py3 --abi-tag none (Get-ChildItem wheelhouse/*.whl) --remove

As on other platforms, ``PKG_CONFIG_PATH`` must resolve ``vapoursynth.pc``
before invoking the build.

The extra ``colocate_plugin_libs.py`` pass is what makes the plugin autoload.
delvewheel puts the vendored DLLs in a top-level ``vsanalog.libs/`` reachable
only through a patch that runs on ``import vsanalog``, which VapourSynth's
autoloader never does; the script moves them into the plugin's own directory,
where the loader finds them unaided. The last ``--namespace-pkg`` entry names
that directory so delvewheel does not mint an ``__init__.py`` inside it.

GPU-execution-provider wheels are built the same way with ``-Dep=cuda``,
``-Dep=migraphx`` or ``-Dep=directml`` and an ONNX Runtime carrying that
provider. They are too large for PyPI, so they are retagged with a build tag
(``--build 0cuda13``) to keep them installable while distinguishing them from
the CPU wheel.

``-Dep=cuda`` covers both published CUDA variants: the toolkit major is not a
build option but a property of the nvcc and the ONNX Runtime package the build
is pointed at. Our cuFFT kernels and ORT have to agree on ``libcudart``, so a
12.x toolkit is paired with ORT's ``gpu_cuda12`` package and a 13.x toolkit
with ``gpu_cuda13``. Only the wheel's build tag distinguishes the results.

``-Dep=cuda`` and ``-Dep=migraphx`` also switch on libchromadec's native cuFFT
and hipFFT pipelines, so they need the vendor toolkit at build time — nvcc plus
cuFFT, or hipcc plus hipFFT — not just an ONNX Runtime. ``-Dep=directml`` needs
neither, since DirectML inference goes through ONNX Runtime alone.

ONNX Runtime with the MIGraphX provider comes from AMD rather than Microsoft:
the ``onnxruntime-migraphx`` wheel on ROCm's own package index (not
``onnxruntime-rocm``, which carries no MIGraphX provider). It ships shared
objects under ``onnxruntime/capi/`` and no headers, so
``-Dchromadec:onnxruntime_root`` needs a root assembled from those objects and
the headers of Microsoft's matching release.

Versioning
----------
``VERSION.txt`` at the repository root holds the version, in PEP 440 form
(``0.3.0``, ``0.3.0a4``). ``meson.build`` reads it, the distribution metadata
follows from that, ``src/version.h`` is generated from it, and the release
workflow names its tarballs from it — so a bump is a one-line edit to that file.
Meson tracks it, so an edit alone makes the next ``meson compile`` regenerate
the header.

A release tag has to be bumped to match by hand. ``tools/check_version.sh``
compares the two and fails a tag build if they disagree; it also rejects a
``meson.build`` that has gone back to a literal version and a ``src/version.h``
checked in again beside the generated one. Run it any time:

.. code-block:: bash

    tools/check_version.sh            # in-tree copies agree
    tools/check_version.sh v0.3.0     # ...and a tag matches

The plugin's registered version carries only the major and minor components,
since that is all ``VS_MAKE_VERSION`` accepts. The full string, prerelease
suffix included, is available to the plugin as
``VS_ANALOG_PLUGIN_VERSION_STRING``.
