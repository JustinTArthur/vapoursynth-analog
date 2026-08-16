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

With ``--gpu-runtime-dirs``, additionally patches loader search-path entries
pointing at the given site-packages-relative directories (pip's nvidia/* and
tensorrt_libs) onto the injected provider libraries and every plugin binary —
``vsanalog.so`` and any CPU-optimized variant beside it — so their link-time
vendor-runtime dependencies resolve from pip packages even in flows where no
preload ran. Each file keeps the dynamic tag it already has:
auditwheel deliberately stamps the plugin with DT_RPATH (so LD_LIBRARY_PATH
cannot shadow the vendored ONNX Runtime), and flipping it to DT_RUNPATH would
silently change that; a file with no tag gets DT_RUNPATH, which ranks below
LD_LIBRARY_PATH and so cannot override a system runtime the user selected.

    python tools/inject_ort_providers.py <wheel> <lib> [<lib> ...] \
        [--gpu-runtime-dirs nvidia/cu13/lib:tensorrt_libs]
"""

from __future__ import annotations

import argparse
import shutil
import subprocess
import sys
import tempfile
from pathlib import Path

# Where auditwheel puts the libraries it vendors. Windows uses delvewheel's
# own --add-dll instead of this script.
LIBS_DIR_NAME = "vsanalog.libs"

PLUGIN_DIR_REL_PATH = Path("vapoursynth/plugins/vsanalog")

# vsanalog.so plus any CPU-optimized siblings (vsanalog.avx2.so and friends),
# which link the same vendor runtime and so need the same search-path entries.
PLUGIN_GLOB = "vsanalog*.so"


def _rpath_state(lib: Path) -> tuple[str | None, str]:
    """Return (dynamic tag or None, current search path string)."""
    out = subprocess.run(
        ["readelf", "-d", str(lib)], check=True, capture_output=True, text=True,
    )
    for line in out.stdout.splitlines():
        for tag in ("RUNPATH", "RPATH"):
            if f"({tag})" in line:
                value = line[line.index("[") + 1 : line.rindex("]")] if "[" in line else ""
                return tag, value
    return None, ""


def _add_search_path_entries(lib: Path, entries: list[str]) -> None:
    tag, value = _rpath_state(lib)
    current = [e for e in value.split(":") if e]
    for entry in entries:
        if entry not in current:
            current.append(entry)
    cmd = ["patchelf", "--set-rpath", ":".join(current), str(lib)]
    if tag == "RPATH":
        # Keep auditwheel's deliberate DT_RPATH; plain --set-rpath would
        # convert it to DT_RUNPATH and let LD_LIBRARY_PATH shadow the
        # vendored libraries.
        cmd.insert(1, "--force-rpath")
    subprocess.run(cmd, check=True)

    after_tag, after_value = _rpath_state(lib)
    expected_tag = tag if tag is not None else "RUNPATH"
    if after_tag != expected_tag:
        sys.exit(
            f"{lib.name}: dynamic tag changed from {tag or 'none'} to "
            f"{after_tag or 'none'} while patching; refusing to ship a wheel "
            "whose library search precedence silently flipped."
        )
    missing = [e for e in entries if e not in after_value.split(":")]
    if missing:
        sys.exit(f"{lib.name}: entries not present after patching: {missing}")


def main(argv: list[str]) -> int:
    parser = argparse.ArgumentParser(
        description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter,
    )
    parser.add_argument("wheel", type=Path)
    parser.add_argument("libs", nargs="+", type=Path)
    parser.add_argument(
        "--gpu-runtime-dirs",
        help="colon-separated site-packages-relative directories to add as "
        "$ORIGIN-relative RUNPATH entries on the injected libraries and every "
        "plugin binary (Linux GPU wheels only)",
    )
    args = parser.parse_args(argv[1:])

    wheel = args.wheel.resolve()
    libs = [lib.resolve() for lib in args.libs]

    if not wheel.is_file():
        sys.stderr.write(f"wheel not found: {wheel}\n")
        return 1
    missing = [str(lib) for lib in libs if not lib.is_file()]
    if missing:
        sys.stderr.write("provider libraries not found:\n  " + "\n  ".join(missing) + "\n")
        return 1

    runtime_dirs = []
    if args.gpu_runtime_dirs:
        runtime_dirs = [d for d in args.gpu_runtime_dirs.split(":") if d]

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

        if runtime_dirs:
            # vsanalog.libs/ sits at the site-packages root, one level up from
            # the vendor package directories.
            provider_entries = [f"$ORIGIN/../{d}" for d in runtime_dirs]
            for lib in libs:
                _add_search_path_entries(libs_dir / lib.name, provider_entries)

            # The plugin's own link-time vendor dependencies (with_cuda
            # builds' cudart/cufft) resolve at plugin load; from
            # vapoursynth/plugins/vsanalog/, site-packages is three up. A CPU
            # variant is loaded in the baseline's place, so missing it here
            # would fail the pip-only flow on exactly the CPUs that select one.
            plugins = sorted((unpacked / PLUGIN_DIR_REL_PATH).glob(PLUGIN_GLOB))
            if not plugins:
                sys.stderr.write(
                    f"no {PLUGIN_GLOB} found in {PLUGIN_DIR_REL_PATH} in wheel\n"
                )
                return 1
            plugin_entries = [f"$ORIGIN/../../../{d}" for d in runtime_dirs]
            for plugin in plugins:
                _add_search_path_entries(plugin, plugin_entries)

        out_dir = wheel.parent
        wheel.unlink()
        subprocess.run(
            [sys.executable, "-m", "wheel", "pack", "-d", str(out_dir), str(unpacked)],
            check=True,
        )

    injected = ", ".join(lib.name for lib in libs)
    note = f" (+ RUNPATH entries for {':'.join(runtime_dirs)})" if runtime_dirs else ""
    print(f"injected {injected} into {wheel.name}{note}")
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
