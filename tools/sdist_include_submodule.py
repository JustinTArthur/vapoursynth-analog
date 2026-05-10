#!/usr/bin/env python3
"""Prune unwanted files from the tbc-tools submodule before sdist sealing.

Modern ``meson dist`` recurses into git submodules and stages every
tracked file, including the upstream fork's 5+ GB Windows vcpkg binary
cache under ``ci/cache/``. We don't compile against any of that and PyPI
has hard size limits, so we prune the staged submodule down to just what
the build actually needs.

Run automatically via ``meson.add_dist_script()`` from the top-level
``meson.build``. The script operates on ``MESON_PROJECT_DIST_ROOT``,
which is the staging directory meson will tar up after dist scripts
finish.
"""
from __future__ import annotations

import os
import shutil
import sys
from pathlib import Path

# Submodule entries we keep. Anything else under extern/tbc-tools/ is
# deleted from the staged dist tree before the tarball is created.
KEEP = frozenset({
    "src",      # C++ sources we compile
    "LICENSE",  # GPL-3 text travels with the source
})


def main():
    try:
        dist_root = Path(os.environ["MESON_PROJECT_DIST_ROOT"])
    except KeyError as exc:
        sys.stderr.write(f"missing meson env var: {exc}\n")
        return 1

    submodule_dst = dist_root / "extern" / "tbc-tools"

    if not submodule_dst.is_dir():
        # No submodule staged (e.g. shallow checkout missing it). Nothing
        # to prune; the build will fail later with a clearer message.
        return 0

    pruned = []
    for entry in sorted(submodule_dst.iterdir()):
        if entry.name in KEEP:
            continue
        if entry.is_dir():
            shutil.rmtree(entry)
        else:
            entry.unlink()
        pruned.append(entry.name)

    print(
        f"pruned extern/tbc-tools entries from sdist: {', '.join(pruned)}"
        if pruned
        else "extern/tbc-tools sdist staging already minimal",
        flush=True,
    )
    return 0


if __name__ == "__main__":
    sys.exit(main())