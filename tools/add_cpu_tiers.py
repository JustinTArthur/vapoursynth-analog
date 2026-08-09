#!/usr/bin/env python3
"""Add CPU-optimised plugin variants to a wheel for VapourSynth to choose between.

VapourSynth R75+ probes for siblings of a manifest-declared plugin named
``<plugin>.<tier>.<ext>`` and loads the best one the running CPU supports,
walking down from the CPU's ABI level until a variant exists. So a wheel
carrying ``vsanalog.dll`` plus ``vsanalog.avx2.dll`` runs the AVX2 build on
capable hardware and the baseline build everywhere else, with no dispatch code.

The variants are plain copies of the plugin built with a higher ``-march``;
they are NOT extra manifest entries. Listing one in ``manifest.vs`` would load
it unconditionally on every CPU, including ones that would fault on it.

Run this BEFORE ``delvewheel``/``auditwheel repair``. Those tools rename the
vendored dependencies and patch the importing binaries to match; a variant
injected afterwards would still reference the unmangled names and fail to load.
"""
from __future__ import annotations

import argparse
import shutil
import subprocess
import sys
import tempfile
from pathlib import Path

# Suffixes VapourSynth actually probes for, in descending ABI level. Anything
# else would be inert: the file would ship but never be looked for.
KNOWN_TIERS = ("zn4", "avx512", "avx2")

# The probe is compiled under VS_TARGET_CPU_X86 only, so a variant in an ARM
# wheel is dead weight that nothing will ever load.
X86_TAG_MARKERS = ("x86_64", "amd64", "win32", "i686")

PLUGIN_SUBDIR = ("vapoursynth", "plugins", "vsanalog")


def _fail(msg: str) -> int:
    sys.stderr.write(f"error: {msg}\n")
    return 1


def main(argv: list[str]) -> int:
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("wheel")
    ap.add_argument("output_dir")
    ap.add_argument(
        "--tier",
        action="append",
        default=[],
        metavar="NAME=PATH",
        help=f"variant to add, e.g. avx2=build_avx2/vsanalog.dll. One of: {', '.join(KNOWN_TIERS)}",
    )
    args = ap.parse_args(argv[1:])

    wheel = Path(args.wheel).resolve()
    out_dir = Path(args.output_dir).resolve()
    if not wheel.is_file():
        return _fail(f"wheel not found: {wheel}")
    if not args.tier:
        return _fail("no --tier given")

    tiers: dict[str, Path] = {}
    for spec in args.tier:
        name, _, path = spec.partition("=")
        if not path:
            return _fail(f"malformed --tier {spec!r}, expected NAME=PATH")
        if name not in KNOWN_TIERS:
            return _fail(
                f"unknown tier {name!r}; VapourSynth only probes for {', '.join(KNOWN_TIERS)}"
            )
        binary = Path(path).resolve()
        if not binary.is_file():
            return _fail(f"tier binary not found: {binary}")
        tiers[name] = binary

    # A variant only ever loads on x86; refuse rather than silently bloat.
    if not any(m in wheel.name for m in X86_TAG_MARKERS):
        return _fail(
            f"{wheel.name} is not an x86 wheel; VapourSynth's tier probe is x86-only"
        )

    out_dir.mkdir(parents=True, exist_ok=True)

    with tempfile.TemporaryDirectory() as td:
        tmp = Path(td)
        subprocess.run(
            [sys.executable, "-m", "wheel", "unpack", "-d", str(tmp), str(wheel)],
            check=True,
        )
        unpacked = next(p for p in tmp.iterdir() if p.is_dir())
        plugin_dir = unpacked.joinpath(*PLUGIN_SUBDIR)
        if not plugin_dir.is_dir():
            return _fail(f"{'/'.join(PLUGIN_SUBDIR)} not found in {wheel.name}")

        base = next(
            (
                p
                for p in plugin_dir.iterdir()
                if p.is_file() and p.stem == "vsanalog" and p.suffix in {".dll", ".so", ".dylib"}
            ),
            None,
        )
        if base is None:
            return _fail(f"no vsanalog.{{dll,so,dylib}} in {'/'.join(PLUGIN_SUBDIR)}")

        # Without a manifest the plugin is loaded by the directory scan, which
        # passes loadCPUOptimized=false — the variants would never be probed.
        if not (plugin_dir / "manifest.vs").is_file():
            return _fail(
                "manifest.vs missing; without it VapourSynth never probes for CPU variants"
            )

        added = []
        for name, binary in sorted(tiers.items()):
            if binary.read_bytes() == base.read_bytes():
                return _fail(
                    f"tier {name!r} is byte-identical to the baseline plugin — "
                    "the -march flags almost certainly did not take effect"
                )
            dest = plugin_dir / f"vsanalog.{name}{base.suffix}"
            shutil.copy2(binary, dest)
            added.append(f"{dest.name} ({dest.stat().st_size:,} bytes)")

        subprocess.run(
            [sys.executable, "-m", "wheel", "pack", "-d", str(out_dir), str(unpacked)],
            check=True,
        )

    print(f"added CPU variant(s) beside {base.name}: {', '.join(added)}")
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
