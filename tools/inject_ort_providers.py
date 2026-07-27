#!/usr/bin/env python3
"""Inject ONNX Runtime execution-provider libraries into a built wheel.

ORT keeps every non-CPU execution provider in its own shared library and loads
it with dlopen/LoadLibrary at session-creation time, resolved next to the main
``libonnxruntime`` it belongs to. Nothing links against those libraries, so
auditwheel and delvewheel — which walk link-time dependencies — never see them
and a repaired GPU wheel ends up carrying the runtime but none of its providers.
The EP attach then fails and inference silently falls through to the CPU.

This copies the provider libraries into the wheel's bundled-library directory
and repacks so RECORD checksums stay valid.

    python tools/inject_ort_providers.py <wheel> <lib> [<lib> ...]
"""

from __future__ import annotations

import shutil
import subprocess
import sys
import tempfile
from pathlib import Path

# Where auditwheel puts the libraries it vendors. Windows uses delvewheel's
# own --add-dll instead of this script.
LIBS_DIR_NAME = "vsanalog.libs"


def main(argv: list[str]) -> int:
    if len(argv) < 3:
        sys.stderr.write(f"usage: {argv[0]} <wheel> <lib> [<lib> ...]\n")
        return 2

    wheel = Path(argv[1]).resolve()
    libs = [Path(a).resolve() for a in argv[2:]]

    if not wheel.is_file():
        sys.stderr.write(f"wheel not found: {wheel}\n")
        return 1
    missing = [str(lib) for lib in libs if not lib.is_file()]
    if missing:
        sys.stderr.write("provider libraries not found:\n  " + "\n  ".join(missing) + "\n")
        return 1

    with tempfile.TemporaryDirectory() as td:
        tmp = Path(td)
        subprocess.run(
            [sys.executable, "-m", "wheel", "unpack", "-d", str(tmp), str(wheel)],
            check=True,
        )
        unpacked = next(p for p in tmp.iterdir() if p.is_dir())

        libs_dir = unpacked / LIBS_DIR_NAME
        if not libs_dir.is_dir():
            sys.stderr.write(
                f"no {LIBS_DIR_NAME}/ in {wheel.name}; the wheel was never repaired\n"
            )
            return 1

        for lib in libs:
            shutil.copy2(lib, libs_dir / lib.name)

        out_dir = wheel.parent
        wheel.unlink()
        subprocess.run(
            [sys.executable, "-m", "wheel", "pack", "-d", str(out_dir), str(unpacked)],
            check=True,
        )

    print(f"injected {', '.join(lib.name for lib in libs)} into {wheel.name}")
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
