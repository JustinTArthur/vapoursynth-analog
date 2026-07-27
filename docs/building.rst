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
- `FFTW3 <http://www.fftw.org/>`_, for the Transform PAL decoders. Without it
  the build succeeds and those decoders are simply absent; ``meson setup``
  reports ``FFTW3 (Transform-PAL)`` in libchromadec's feature summary.

SQLite is *not* a build dependency: libchromadec ships a trimmed amalgamation
that this project always builds against (``force_fallback_for=sqlite3`` in
``meson.build``), so the plugin reads ``.db`` metadata sidecars without linking
a system libsqlite3. Qt is no longer used at all.

The neural-network decoders need a backend, selected by the ``ep``
(execution provider) option — see `Neural-Network Backends`_ below. On macOS
they are always built against the system CoreML frameworks; elsewhere they need
`ONNX Runtime <https://onnxruntime.ai/>`_ and are omitted with ``-Dep=none``.

Installing Build Dependencies
^^^^^^^^^^^^^^^^^^^^^^^^^^^^^

**macOS (Homebrew):**

.. code-block:: bash

    brew install meson ninja fftw vapoursynth

**Ubuntu/Debian:**

.. code-block:: bash

    sudo apt install meson ninja-build build-essential git libfftw3-dev vapoursynth-dev

**Fedora:**

.. code-block:: bash

    sudo dnf install meson ninja-build gcc-c++ git fftw-devel vapoursynth-devel

Add ``fftw-static`` if you want FFTW linked statically (see
`Self-Contained Plugin Builds`_).

**Arch Linux:**

.. code-block:: bash

    sudo pacman -S meson ninja base-devel git fftw vapoursynth

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

Self-Contained Plugin Builds
^^^^^^^^^^^^^^^^^^^^^^^^^^^^
A plain build links FFTW dynamically, so the plugin you copy elsewhere needs
the FFTW shared library on the target machine. Adding ``-Dprefer_static=true``
takes the static archives instead where they exist, which is what the release
tarballs are built with:

.. code-block:: bash

    meson setup build -Dep=none -Dprefer_static=true
    meson compile -C build

The result depends on nothing outside the C++ runtime, VapourSynth, and — on
macOS — the system CoreML frameworks. This needs a static FFTW to be installed
(``libfftw3.a``: Homebrew's ``fftw`` includes it, Fedora/RHEL split it into an
``fftw-static`` package, Debian/Ubuntu ship it in ``libfftw3-dev``).

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

It carries the libchromadec wrap rather than libchromadec itself, so building
from it fetches the subproject over the network. It carries no model weights
either: install from an sdist and the neural-network decoders need models you
supply yourself.

The resulting ``dist/vsanalog-<version>.tar.gz`` can be uploaded to PyPI or
used as input to a wheel build on another machine.

Wheel
~~~~~
Building a wheel compiles the native plugin, so the same build dependencies
listed above must be available. Whatever shared libraries the plugin ended up
linking (ONNX Runtime, and FFTW unless it was linked statically) are not
present on end-user machines, so the platform-appropriate repair tool vendors
them into the wheel.

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

``PKG_CONFIG_PATH`` should resolve ``vapoursynth.pc`` and ``fftw3.pc`` —
``/opt/homebrew/lib/pkgconfig`` on Apple silicon.

**Linux (manylinux):**

Linux wheels are built inside a ``quay.io/pypa/manylinux_2_28_x86_64`` (or
``manylinux_2_34_aarch64``) container so that the resulting binaries are
compatible with older glibc versions. Inside the container, install the build
dependencies (``fftw-devel``, ``fftw-static``, ``gcc-c++``, ``git``, an
unpacked ONNX Runtime, and the VapourSynth headers), then:

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

Windows wheels need an MSVC developer environment, vcpkg-provided FFTW3, and an
unpacked ONNX Runtime. ``delvewheel`` vendors the DLLs from vcpkg's installed
tree and the ONNX Runtime directory:

.. code-block:: powershell

    pip install build delvewheel wheel
    python -m build --wheel -o dist/ `
        -Csetup-args="-Dep=cpu" `
        -Csetup-args="-Dchromadec:onnxruntime_root=$env:ORT_ROOT"
    delvewheel repair --analyze-existing --namespace-pkg "vapoursynth;vapoursynth.plugins" `
        --add-path "C:/vcpkg/installed/x64-windows/bin;$env:ORT_ROOT/lib" `
        dist/*.whl -w wheelhouse/
    python -m wheel tags --python-tag py3 --abi-tag none (Get-ChildItem wheelhouse/*.whl) --remove

As on other platforms, ``PKG_CONFIG_PATH`` must resolve ``vapoursynth.pc`` and
``fftw3.pc`` before invoking the build.

GPU-execution-provider wheels are built the same way with ``-Dep=cuda``,
``-Dep=migraphx`` or ``-Dep=directml`` and an ONNX Runtime carrying that
provider. They are too large for PyPI, so they are retagged with a build tag
(``--build 0cuda``) to keep them installable while distinguishing them from the
CPU wheel.

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
