#!/usr/bin/env python3
"""Stamp GPU-runtime extras into a variant wheel's metadata, in place.

The CUDA variant wheels exclude the vendor runtime and resolve pip-installed
copies at load time (the #7 preload chain); the pip packages that satisfy it
are documented prose today. This declares them as extras instead, so
``pip install "vsanalog[tensorrt] @ <url>"`` pins the right set for the wheel
it accompanies.

meson-python cannot do this at build time: PEP 621 ``dynamic`` supports only
version/license there, and every variant shares one pyproject.toml while
needing different pins (CUDA 12 keeps the ``-cu12`` package suffix; CUDA 13
dropped it, except cuDNN). So the requirement sets live here, keyed by
variant, and CI stamps each wheel after its build-tag retag. Bump them
together with the preload table in meson.build — both encode the majors the
bundled ONNX Runtime loads.

The ``[tensorrt]`` extra repeats the ``[cuda]`` set rather than using a
self-referential ``vsanalog[cuda]`` marker: the variant wheels are not on any
index, and a resolver asked for ``vsanalog`` by name may look past the
direct-URL candidate. TensorRT's PyPI packages are wheel-stub sdists that
fetch the real wheel from pypi.nvidia.com at install time, so they resolve
with no extra index but do not survive ``--only-binary`` / hash-pinned flows.

Usage:
    python tools/add_variant_extras.py <wheel> --variant cuda12|cuda13
"""

from __future__ import annotations

import argparse
import shutil
import subprocess
import sys
import tempfile
import zipfile
from pathlib import Path

_CUDA12 = [
    "nvidia-cuda-runtime-cu12==12.*",
    "nvidia-cuda-nvrtc-cu12==12.*",
    "nvidia-cublas-cu12==12.*",
    "nvidia-curand-cu12==10.*",
    "nvidia-cufft-cu12==11.*",
    "nvidia-cudnn-cu12==9.*",
]
# Unsuffixed names carry the cu13+ builds, versioned by each library's own
# version (cudart 13.x, cufft 12.x, curand 10.x). nvidia-cuda-runtime-cu13
# exists on PyPI only as an empty placeholder — do not "fix" these back to
# suffixed names. cuDNN alone kept the suffix.
_CUDA13 = [
    "nvidia-cuda-runtime==13.*",
    "nvidia-cuda-nvrtc==13.*",
    "nvidia-cublas==13.*",
    "nvidia-curand==10.*",
    "nvidia-cufft==12.*",
    "nvidia-cudnn-cu13==9.*",
]

# TensorRT majors: the bundled ORT loads libnvinfer.so.10 / nvinfer_10.dll,
# and an unbounded install gets 11.x, which the preload pin never picks up.
# CUDA 13 TensorRT wheels start at 10.13.
VARIANT_EXTRAS = {
    "cuda12": {
        "cuda": _CUDA12,
        "tensorrt": _CUDA12 + ["tensorrt-cu12-libs>=10,<11"],
    },
    "cuda13": {
        "cuda": _CUDA13,
        "tensorrt": _CUDA13 + ["tensorrt-cu13-libs>=10.13,<11"],
    },
}


def _inject(metadata_path: Path, extras: dict[str, list[str]]) -> list[str]:
    text = metadata_path.read_text(encoding="utf-8")
    head, sep, body = text.partition("\n\n")
    if any(line.startswith("Provides-Extra:") for line in head.splitlines()):
        sys.exit(f"{metadata_path}: wheel already declares extras")
    added = []
    for name, requirements in extras.items():
        added.append(f"Provides-Extra: {name}")
        for req in requirements:
            added.append(f'Requires-Dist: {req} ; extra == "{name}"')
    return_text = head + "\n" + "\n".join(added) + sep + body
    metadata_path.write_text(return_text, encoding="utf-8")
    return added


def main(argv: list[str]) -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("wheel", type=Path)
    parser.add_argument("--variant", choices=sorted(VARIANT_EXTRAS), required=True)
    args = parser.parse_args(argv[1:])

    wheel = args.wheel.resolve()
    extras = VARIANT_EXTRAS[args.variant]

    with tempfile.TemporaryDirectory() as td:
        subprocess.run(
            [sys.executable, "-m", "wheel", "unpack", str(wheel), "-d", td],
            check=True,
        )
        (unpacked,) = [p for p in Path(td).iterdir() if p.is_dir()]
        (metadata,) = unpacked.glob("*.dist-info/METADATA")
        added = _inject(metadata, extras)

        # wheel pack rebuilds RECORD and keeps the build tag from WHEEL, so
        # the packed filename matches the retagged input's.
        outdir = Path(td) / "out"
        outdir.mkdir()
        subprocess.run(
            [sys.executable, "-m", "wheel", "pack", str(unpacked), "-d", str(outdir)],
            check=True,
        )
        (packed,) = outdir.glob("*.whl")
        if packed.name != wheel.name:
            sys.exit(
                f"packed name {packed.name} != input {wheel.name}; refusing to "
                "replace a wheel whose tags changed in repack"
            )
        with zipfile.ZipFile(packed) as zf:
            (meta_name,) = [n for n in zf.namelist() if n.endswith(".dist-info/METADATA")]
            final = zf.read(meta_name).decode("utf-8")
        missing = [line for line in added if line not in final]
        if missing:
            sys.exit(f"metadata lines lost in repack: {missing}")
        shutil.move(str(packed), str(wheel))

    print(f"{wheel.name}: declared extras " + ", ".join(sorted(extras)))
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
