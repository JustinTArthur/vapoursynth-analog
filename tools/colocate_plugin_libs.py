#!/usr/bin/env python3
"""Co-locate a wheel's bundled DLLs with the vsanalog VapourSynth plugin.

delvewheel vendors a plugin's dependency DLLs into a top-level ``vsanalog.libs/``
directory and patches ``vsanalog/__init__.py`` to add that directory to the DLL
search path when the ``vsanalog`` Python package is imported. VapourSynth's
plugin autoloader loads ``vsanalog.dll`` directly, without importing that
package, so the patch never runs and the dependencies are not found.

This script moves the vendored DLLs next to ``vsanalog.dll`` inside
``vapoursynth/plugins/vsanalog/``. VapourSynth loads plugins with
``LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR``, which searches the plugin's own directory
for its dependencies, so co-located DLLs resolve with no Python import. The
sibling ``manifest.vs`` keeps the autoloader from treating those DLLs as
plugins. The now-redundant delvewheel search-path patch is dropped by restoring
the pristine wrapper module, and ``wheel pack`` rebuilds RECORD checksums.

Dropping the patch rather than leaving it inert matters: it names a directory
this repack deletes, so a stale ``vsanalog.libs/`` surviving on disk from a
half-removed older install would be put back on the DLL search path, where its
DLLs could win over the co-located ones.

The plugin's import library goes too. Meson installs it beside the DLL and
offers no way to opt out — the implib install ignores ``install_dir`` and is
hardcoded — but nothing links against a VapourSynth plugin, so in a wheel it is
dead weight.

Windows-only: Linux and macOS bake an RPATH into the plugin binary, so their
dependencies resolve without co-location.
"""
from __future__ import annotations

import shutil
import subprocess
import sys
import tempfile
from pathlib import Path

# Pristine wrapper module, used to overwrite delvewheel's patched copy.
_PRISTINE_INIT = (
    Path(__file__).resolve().parent.parent / "python" / "vsanalog" / "__init__.py"
)


def main(argv):
    if len(argv) != 3:
        sys.stderr.write(f"usage: {argv[0]} <wheel> <output_dir>\n")
        return 2

    wheel = Path(argv[1]).resolve()
    out_dir = Path(argv[2]).resolve()
    if not wheel.is_file():
        sys.stderr.write(f"wheel not found: {wheel}\n")
        return 1
    if not _PRISTINE_INIT.is_file():
        sys.stderr.write(f"pristine __init__.py not found: {_PRISTINE_INIT}\n")
        return 1

    out_dir.mkdir(parents=True, exist_ok=True)

    with tempfile.TemporaryDirectory() as td:
        tmp = Path(td)
        subprocess.run(
            [sys.executable, "-m", "wheel", "unpack", "-d", str(tmp), str(wheel)],
            check=True,
        )
        unpacked = next(p for p in tmp.iterdir() if p.is_dir())

        libs_dir = unpacked / "vsanalog.libs"
        plugin_dir = unpacked / "vapoursynth" / "plugins" / "vsanalog"
        if not libs_dir.is_dir():
            sys.stderr.write(f"vsanalog.libs/ not found in {wheel}\n")
            return 1
        if not (plugin_dir / "vsanalog.dll").is_file():
            sys.stderr.write(
                f"vapoursynth/plugins/vsanalog/vsanalog.dll not found in {wheel}\n"
            )
            return 1
        # Without the manifest the autoloader would probe every co-located DLL
        # as a plugin, so treat a missing one as a build error rather than
        # shipping a wheel that silently loads tens of megabytes per core.
        if not (plugin_dir / "manifest.vs").is_file():
            sys.stderr.write(
                f"vapoursynth/plugins/vsanalog/manifest.vs not found in {wheel}\n"
            )
            return 1

        moved = sorted(dll.name for dll in libs_dir.glob("*.dll"))
        if not moved:
            sys.stderr.write(f"no DLLs to co-locate in {libs_dir.name}/\n")
            return 1
        for name in moved:
            shutil.move(str(libs_dir / name), str(plugin_dir / name))
        shutil.rmtree(libs_dir)
        shutil.copy2(_PRISTINE_INIT, unpacked / "vsanalog" / "__init__.py")

        dropped = sorted(lib.name for lib in plugin_dir.glob("*.lib"))
        for name in dropped:
            (plugin_dir / name).unlink()

        subprocess.run(
            [sys.executable, "-m", "wheel", "pack", "-d", str(out_dir), str(unpacked)],
            check=True,
        )

    print(f"co-located {len(moved)} DLL(s) with the plugin: {', '.join(moved)}")
    if dropped:
        print(f"dropped {len(dropped)} import librar(y/ies): {', '.join(dropped)}")
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
