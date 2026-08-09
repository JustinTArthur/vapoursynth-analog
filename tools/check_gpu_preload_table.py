#!/usr/bin/env python3
"""Verify the GPU preload table covers what the shipped binaries actually need.

The preload table (one Meson source generating src/gpupreload_config.h and the
wheel's _vsanalog_gpu_preload.py) hardcodes the vendor-runtime sonames the
ONNX Runtime execution providers and the plugin resolve at load time. Those
sets change only when the bundled ORT (or the CUDA toolkit major) moves — the
exact moment a stale table would otherwise fail silently at runtime on user
machines CI cannot represent.

Two checks, because the table has two kinds of entry:

1. Link-time deps, read as DT_NEEDED off the installed binaries.
2. Runtime-dlopened families. A loader shim like libcudnn.so.9 dlopens its
   sublibraries by bare soname, so they appear in no DT_NEEDED anywhere and
   check 1 is blind to them. Missing ones surface only on a GPU, as an
   inference failure inside a kernel rather than as a missing library — for
   nnTransform3D that is a silently 2D-decoded frame. So when the pip package
   supplying a family is installed, every library it ships is required to be
   in the table, with the pip layout as the source of truth.

Run inside the GPU wheel container after `pip install <wheel>`, with the pip
vendor packages installed for check 2 to have anything to compare against:

    python tools/check_gpu_preload_table.py
"""

from __future__ import annotations

import subprocess
import sys
import sysconfig
from pathlib import Path

# Anything with one of these prefixes is the user-supplied vendor runtime; the
# rest of DT_NEEDED (glibc, libstdc++, the vendored ORT) is the wheel's own
# business. libcuda/libnvidia-* are the driver: never pip-installable, loaded
# by its own probe, deliberately absent from the table.
VENDOR_PREFIXES = (
    "libcudart", "libcublas", "libcurand", "libcufft", "libnvrtc",
    "libcudnn", "libnvinfer", "libnvonnxparser", "libamdhip", "libhipfft",
    "libmigraphx",
)
DRIVER_PREFIXES = ("libcuda.so", "libnvidia-",)

# Families whose members are dlopened by bare soname at runtime, keyed by the
# site-packages-relative directory of the pip package that ships them. Every
# matching file in an installed one has to be in the table.
DLOPEN_FAMILIES = (
    ("nvidia/cudnn/lib", "libcudnn*.so.9"),
    ("nvidia/cu13/lib", "libcudnn*.so.9"),
)


def needed(lib: Path) -> set[str]:
    out = subprocess.run(
        ["readelf", "-d", str(lib)], check=True, capture_output=True, text=True,
    )
    result = set()
    for line in out.stdout.splitlines():
        if "(NEEDED)" in line and "[" in line:
            result.add(line[line.index("[") + 1 : line.rindex("]")])
    return result


def main() -> int:
    try:
        import _vsanalog_gpu_preload as preload
    except ImportError:
        sys.exit("no _vsanalog_gpu_preload installed; not a GPU wheel environment")
    table_names = {name for name, _ in preload._PRELOAD_TABLE}
    # The dlopen families are the plugin's to load, not the startup module's,
    # but both tables come from the same Meson source and both are checked.
    dlopen_names = {name for name, _ in getattr(preload, "_DLOPEN_TABLE", ())}

    site = Path(sysconfig.get_paths()["purelib"])
    binaries = [site / "vapoursynth" / "plugins" / "vsanalog" / "vsanalog.so"]
    binaries += sorted((site / "vsanalog.libs").glob("libonnxruntime_providers_*.so"))

    demanded = set()
    for binary in binaries:
        if not binary.is_file():
            sys.exit(f"expected wheel binary missing: {binary}")
        for soname in needed(binary):
            if soname.startswith(DRIVER_PREFIXES):
                continue
            if soname.startswith(VENDOR_PREFIXES):
                demanded.add(soname)

    missing = sorted(demanded - table_names)
    if missing:
        sys.exit(
            "GPU preload table is missing vendor libraries the shipped "
            f"binaries link against: {', '.join(missing)}. Update the table "
            "in meson.build (gpu_preload_entries) to match the bundled ONNX "
            "Runtime's needs."
        )

    unused = sorted(table_names - demanded)
    print(f"preload table covers all {len(demanded)} linked vendor libraries")
    if unused:
        # Runtime-dlopened libraries (nvrtc via cuDNN's JIT engines) never
        # appear in DT_NEEDED, so extras are informational, not errors.
        print(f"table entries beyond link-time needs (runtime-loaded): {', '.join(unused)}")

    checked_families = 0
    for reldir, pattern in DLOPEN_FAMILIES:
        family_dir = site / reldir
        if not family_dir.is_dir():
            continue
        shipped = {p.name for p in family_dir.glob(pattern)}
        if not shipped:
            continue
        checked_families += 1
        absent = sorted(shipped - table_names - dlopen_names)
        if absent:
            sys.exit(
                f"GPU preload table is missing runtime-dlopened libraries that "
                f"{reldir} ships: {', '.join(absent)}. A loader shim resolves "
                "these by bare soname, so leaving them out fails on a GPU as an "
                "inference error, not as a missing library. Add them to "
                "gpu_dlopen_entries in meson.build, leaves first."
            )
        print(f"preload table covers all {len(shipped)} libraries in {reldir}")
    if checked_families == 0:
        print("no dlopen-family pip packages installed; that coverage went unchecked")
    return 0


if __name__ == "__main__":
    sys.exit(main())
