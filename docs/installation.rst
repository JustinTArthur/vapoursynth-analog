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
Python module. The wheel bundles its shared library dependencies, so no
additional runtime libraries need to be installed separately. Only
VapourSynth itself is required.

Manual Plugin Installation
--------------------------
Alternatively, obtain or build the plugin for your operating system and place
``vsanalog.dll``, ``vsanalog.dylib``, or ``vsanalog.so`` into your VapourSynth
autoloading plugins directory.

Runtime Dependencies
~~~~~~~~~~~~~~~~~~~~
A manual install requires the following libraries to be present on your
system:

- **VapourSynth** (>= R55)
- **Qt6** (Core module)
- **FFTW3**
- **SQLite3**

**macOS (Homebrew):**

.. code-block:: bash

    brew install qt6 fftw sqlite

**Ubuntu/Debian:**

.. code-block:: bash

    sudo apt install libqt6core6 libfftw3-3 libsqlite3-0

**Fedora:**

.. code-block:: bash

    sudo dnf install qt6-qtbase fftw-libs sqlite-libs

**Arch Linux:**

.. code-block:: bash

    sudo pacman -S qt6-base fftw sqlite

**Windows:**

Install VapourSynth, then ensure Qt6, FFTW3, and SQLite3 DLLs are available in
your PATH or alongside the plugin.