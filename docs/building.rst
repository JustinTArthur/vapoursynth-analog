Building from Source
====================
This guide covers two separate build paths:

- Building the native VapourSynth plugin on its own, to be dropped into a
  VapourSynth plugins directory.
- Building Python distributions (sdist and wheel) that bundle the plugin
  together with the ``vsanalog`` Python wrapper for installation via ``pip``.

Building the VapourSynth Plugin
-------------------------------
The steps in this section produce a standalone plugin binary
(``vsanalog.dll`` / ``.dylib`` / ``.so``) for manual installation. If you only
intend to build a Python wheel for redistribution, skip ahead to
`Building Python Distributions`_ — those instructions invoke this build
internally.

Build Dependencies
~~~~~~~~~~~~~~~~~~
- `Meson <https://mesonbuild.com/>`_ build system
- C++20 compiler (GCC 10+, Clang 12+, or MSVC 2019+)
- `VapourSynth <https://www.vapoursynth.com/>`_ (>= R55) development headers
- `Qt6 <https://www.qt.io/>`_ (Core module)
- `FFTW3 <http://www.fftw.org/>`_
- `SQLite3 <https://www.sqlite.org/>`_

Installing Build Dependencies
^^^^^^^^^^^^^^^^^^^^^^^^^^^^^

**macOS (Homebrew):**

.. code-block:: bash

    brew install meson qt6 fftw sqlite vapoursynth

**Ubuntu/Debian:**

.. code-block:: bash

    sudo apt install meson build-essential qt6-base-dev libfftw3-dev libsqlite3-dev vapoursynth-dev

**Fedora:**

.. code-block:: bash

    sudo dnf install meson gcc-c++ qt6-qtbase-devel fftw-devel sqlite-devel vapoursynth-devel

**Arch Linux:**

.. code-block:: bash

    sudo pacman -S meson qt6-base fftw sqlite vapoursynth

Compiling the Plugin
~~~~~~~~~~~~~~~~~~~~

.. code-block:: bash

    git clone --recursive https://github.com/JustinTArthur/vapoursynth-analog.git
    cd vapoursynth-analog
    meson setup build
    meson compile -C build

The plugin will be built as:

- ``build/vsanalog.dylib`` (macOS)
- ``build/vsanalog.so`` (Linux)
- ``build/vsanalog.dll`` (Windows)

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
An sdist bundles the source tree (including the ld-decode-tools submodule) and
is platform independent. Make sure submodules are checked out, then:

.. code-block:: bash

    git submodule update --init --recursive
    python -m build --sdist

The resulting ``dist/vsanalog-<version>.tar.gz`` can be uploaded to PyPI or
used as input to a wheel build on another machine.

Wheel
~~~~~
Building a wheel compiles the native plugin, so the same build dependencies
listed above must be available. The raw output of ``python -m build --wheel``
links against system copies of Qt6, FFTW3, and SQLite3, which will not be
present on end-user machines. To produce a redistributable wheel, the
platform-appropriate repair tool is run to vendor those shared libraries into
the wheel.

After repair, the wheels are re-tagged as ``py3-none`` because the plugin does
not embed CPython's ABI:

.. code-block:: bash

    python -m wheel tags --python-tag py3 --abi-tag none <wheel> --remove

**macOS:**

.. code-block:: bash

    pip install build delocate wheel
    python -m build --wheel -o dist/
    DYLD_LIBRARY_PATH="$QT_ROOT_DIR/lib" \
        delocate-wheel -w wheelhouse/ dist/*.whl
    python -m wheel tags --python-tag py3 --abi-tag none wheelhouse/*.whl --remove

``PKG_CONFIG_PATH`` should point at your Qt6 ``lib/pkgconfig`` directory so
meson can locate Qt during the build.

**Linux (manylinux):**

Linux wheels are built inside a ``quay.io/pypa/manylinux_2_28_x86_64`` (or
``manylinux_2_34_aarch64``) container so that the resulting binaries are
compatible with older glibc versions. Inside the container, install the build
dependencies (``fftw-devel``, ``sqlite-devel``, Qt6 via ``aqtinstall``, and
VapourSynth headers), then:

.. code-block:: bash

    pip install build auditwheel wheel meson ninja meson-python
    python -m build --wheel --no-isolation -o /tmp/dist/
    auditwheel repair --plat manylinux_2_28_x86_64 -w wheelhouse/ /tmp/dist/*.whl
    python -m wheel tags --python-tag py3 --abi-tag none wheelhouse/*.whl --remove

See ``.github/workflows/build.yml`` for the full container setup used in CI.

**Windows:**

Windows wheels need an MSVC developer environment, Qt6, and vcpkg-provided
FFTW3 and SQLite3. ``delvewheel`` vendors the DLLs from both Qt's ``bin``
directory and vcpkg's installed tree:

.. code-block:: powershell

    pip install build delvewheel wheel
    python -m build --wheel -o dist/
    delvewheel repair `
        --add-path "$env:QT_ROOT_DIR/bin;C:/vcpkg/installed/x64-windows/bin" `
        dist/*.whl -w wheelhouse/
    python -m wheel tags --python-tag py3 --abi-tag none (Get-ChildItem wheelhouse/*.whl) --remove

As on other platforms, ``PKG_CONFIG_PATH`` must resolve ``vapoursynth.pc``,
``fftw3.pc``, ``sqlite3.pc``, and the Qt6 ``.pc`` files before invoking the
build.