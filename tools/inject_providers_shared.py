#!/usr/bin/env python3
"""Inject libonnxruntime_providers_shared.so into the bundled libs dir of a wheel.

ONNX Runtime loads its non-CPU execution providers (CUDA, TensorRT, etc.) as
sibling shared libraries via dlopen. ``libonnxruntime_providers_shared.so`` is
the small bridge that the providers and the main lib both link against. Users
who install ``onnxruntime-gpu`` separately and drop ``libonnxruntime_providers_cuda.so``
next to our bundled libs need this bridge present to make the system load.

auditwheel won't pick it up automatically because it's not a build-time link
dependency. This script extracts the wheel, copies the file into ``vsanalog.libs/``,
and uses ``wheel pack`` to rebuild RECORD checksums.
"""
from __future__ import annotations

import shutil
import subprocess
import sys
import tempfile
from pathlib import Path


def main(argv):
    if len(argv) != 3:
        sys.stderr.write(f"usage: {argv[0]} <wheel> <providers_shared_lib>\n")
        return 2

    wheel = Path(argv[1]).resolve()
    extra = Path(argv[2]).resolve()
    if not wheel.is_file():
        sys.stderr.write(f"wheel not found: {wheel}\n")
        return 1
    if not extra.is_file():
        sys.stderr.write(f"providers_shared lib not found: {extra}\n")
        return 1

    with tempfile.TemporaryDirectory() as td:
        tmp = Path(td)
        subprocess.run(
            [sys.executable, "-m", "wheel", "unpack", "-d", str(tmp), str(wheel)],
            check=True,
        )
        unpacked = next(p for p in tmp.iterdir() if p.is_dir())
        libs_dir = unpacked / "vsanalog.libs"
        if not libs_dir.is_dir():
            sys.stderr.write(f"vsanalog.libs/ not found in {wheel}\n")
            return 1
        shutil.copy2(extra, libs_dir / extra.name)
        out_dir = wheel.parent
        wheel.unlink()
        subprocess.run(
            [sys.executable, "-m", "wheel", "pack", "-d", str(out_dir), str(unpacked)],
            check=True,
        )

    print(f"injected {extra.name} into {wheel.name}")
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))