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

# Top-level submodule entries we keep. Anything else under extern/tbc-tools/
# is deleted from the staged dist tree before the tarball is created.
KEEP_TOP = {"src", "LICENSE"}

# Subdirectories under src/ we actually compile. The rest of src/ (Qt
# GUIs, video exporters, vendored teletext fonts, etc.) gets stripped.
KEEP_SRC = {"library", "ld-chroma-decoder"}


def prune_dir(root: Path, keep: set[str]) -> list[str]:
    pruned = []
    for entry in sorted(root.iterdir()):
        if entry.name in keep:
            continue
        if entry.is_dir():
            shutil.rmtree(entry)
        else:
            entry.unlink()
        pruned.append(entry.name)
    return pruned


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

    pruned_top = prune_dir(submodule_dst, KEEP_TOP)
    src_dst = submodule_dst / "src"
    pruned_src = prune_dir(src_dst, KEEP_SRC) if src_dst.is_dir() else []

    parts = []
    if pruned_top:
        parts.append("top-level: " + ", ".join(pruned_top))
    if pruned_src:
        parts.append("src/: " + ", ".join(pruned_src))
    print(
        "pruned extern/tbc-tools entries from sdist — " + "; ".join(parts)
        if parts
        else "extern/tbc-tools sdist staging already minimal",
        flush=True,
    )
    return 0


if __name__ == "__main__":
    sys.exit(main())