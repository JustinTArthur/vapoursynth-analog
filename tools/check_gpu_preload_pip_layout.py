"""Assert the GPU preload table's directory list matches the pip layout.

tools/check_gpu_preload_table.py checks that the table names the right
libraries. This checks that it names the right *places*: a vendor package can
move its payload between releases — the cu13 wheels put their Windows DLLs a
level below their Linux ones, in ``nvidia/cu13/bin/x86_64`` — and a stale
directory sends every lookup to the "already on the system" fallback, which
succeeds on a machine with a toolkit installed and fails on a user's.

Run in an environment where the wheel is installed through one of its vendor
extras (``pip install "vsanalog[cuda] @ file://..."``):

    python tools/check_gpu_preload_pip_layout.py

Every table entry that the installed pip packages actually ship has to resolve
to that pip copy. Entries whose package is absent are skipped, so the same
check works with or without the [tensorrt] extra.
"""

from __future__ import annotations

import sys
from pathlib import Path

# Where the vendor packages unpack, relative to a site-packages root.
VENDOR_ROOTS = ("nvidia", "tensorrt_libs")


def pip_copies(roots: list[Path]) -> dict[str, Path]:
    """Every shared library the installed vendor packages ship, by file name."""
    found: dict[str, Path] = {}
    for root in roots:
        for vendor in VENDOR_ROOTS:
            base = root / vendor
            if not base.is_dir():
                continue
            for path in base.rglob("*"):
                if path.is_file() and (
                    path.suffix == ".dll" or ".so" in path.name
                ):
                    found.setdefault(path.name, path)
    return found


def main() -> None:
    try:
        import _vsanalog_gpu_preload as preload
    except ImportError:
        sys.exit(
            "_vsanalog_gpu_preload is not installed: this check belongs in an "
            "environment with a GPU wheel installed."
        )

    preload.preload()
    roots = preload._site_roots()
    available = pip_copies(roots)
    if not available:
        sys.exit(
            "no vendor packages found under "
            + ", ".join(f"{r}/{v}" for r in roots for v in VENDOR_ROOTS)
            + ": install the wheel through its [cuda]/[tensorrt] extra first."
        )

    problems = []
    checked = 0
    table = list(preload._PRELOAD_TABLE) + list(preload._DLOPEN_TABLE)
    for name, dirs in table:
        expected = available.get(name)
        if expected is None:
            continue  # the package that ships it is not installed
        checked += 1
        # _DLOPEN_TABLE entries are deliberately not loaded at startup, so
        # check that some listed directory holds the file rather than that it
        # was loaded from there.
        if any((root / Path(*d.split("/")) / name).is_file()
               for root in roots for d in dirs):
            continue
        problems.append(
            f"  {name}: pip ships it at {expected}, but the table looks in "
            + ", ".join(dirs)
        )

    resolved_elsewhere = [
        f"  {name} loaded from {where!r}, not the pip copy at {available[name]}"
        for name, where in preload._loaded.items()
        if name in available and where in ("system", "resident")
    ]
    missing = [f"  {name}: unresolved" for name in preload._missing
               if name in available]

    if problems or resolved_elsewhere or missing:
        sys.exit(
            "the preload table does not match the installed pip layout:\n"
            + "\n".join(problems + resolved_elsewhere + missing)
        )

    print(f"preload table matches the pip layout ({checked} entries checked "
          f"against {len(available)} vendor libraries)")


if __name__ == "__main__":
    main()
