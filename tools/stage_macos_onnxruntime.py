#!/usr/bin/env python3
"""Stage a macOS-friendly copy of libonnxruntime for linking.

Homebrew's ``onnxruntime`` formula ships ``libonnxruntime.A.B.C.dylib`` and a
``libonnxruntime.dylib`` symlink pointing at it -- but no SOVERSION-style
``libonnxruntime.1.dylib``. The shipped dylib's install_name is the absolute
Homebrew path including the patch version, so a binary linked naively against
``-lonnxruntime`` ends up pinned to that exact patch version.

This script copies the real dylib into a staging directory under a stable
``libonnxruntime.1.dylib`` filename, rewrites its ``install_name`` to
``@rpath/libonnxruntime.1.dylib``, and emits a small pkg-config ``.pc`` file
pointing at the staging directory. Linking against this staged copy gets us a
binary that references ``@rpath/libonnxruntime.1.dylib`` -- which delocate-wheel
then bundles into the wheel using that same filename.

The script is idempotent and is invoked from ``meson.build`` at configure time
on macOS.
"""
from __future__ import annotations

import argparse
import os
import shutil
import subprocess
import sys
from pathlib import Path


def find_real_dylib(libdir: Path) -> Path:
    """Return the highest-numbered ``libonnxruntime.X[.Y[.Z]].dylib`` in libdir."""
    candidates = sorted(
        p for p in libdir.glob("libonnxruntime.*.dylib")
        # Skip the bare ``libonnxruntime.dylib`` (no number).
        if p.name != "libonnxruntime.dylib"
    )
    if not candidates:
        raise FileNotFoundError(
            f"No libonnxruntime.*.dylib found in {libdir}"
        )
    # Highest version sorts last lexically when the prefix is fixed.
    return candidates[-1]


def stage_dylib(src: Path, dst: Path) -> bool:
    """Copy src to dst with a rewritten install_name. Returns True if changed."""
    if dst.exists() and dst.stat().st_mtime >= src.stat().st_mtime:
        return False
    dst.parent.mkdir(parents=True, exist_ok=True)
    shutil.copy2(src, dst)
    dst.chmod(0o644)
    subprocess.run(
        [
            "install_name_tool",
            "-id",
            "@rpath/libonnxruntime.1.dylib",
            str(dst),
        ],
        check=True,
    )
    return True


def write_pc(staging: Path, includedir: Path, version: str) -> None:
    pc_dir = staging / "pkgconfig"
    pc_dir.mkdir(parents=True, exist_ok=True)
    pc = pc_dir / "libonnxruntime.pc"
    contents = (
        f"libdir={staging}\n"
        f"includedir={includedir}\n"
        "\n"
        "Name: libonnxruntime\n"
        "Description: ONNX Runtime (vsanalog macOS staging)\n"
        f"Version: {version}\n"
        "Libs: -L${libdir} -lonnxruntime.1\n"
        "Cflags: -I${includedir} -I${includedir}/onnxruntime\n"
    )
    if pc.exists() and pc.read_text() == contents:
        return
    pc.write_text(contents)


def detect_brew_paths() -> tuple[Path, Path, str]:
    """Return (libdir, includedir, version) from a Homebrew ORT install."""
    prefix = subprocess.run(
        ["brew", "--prefix", "onnxruntime"],
        capture_output=True, text=True, check=True,
    ).stdout.strip()
    libdir = Path(prefix) / "lib"
    includedir = Path(prefix) / "include"
    version_line = subprocess.run(
        ["pkg-config", "--modversion", "libonnxruntime"],
        capture_output=True, text=True, check=False,
    )
    version = (
        version_line.stdout.strip() if version_line.returncode == 0 else "1.0.0"
    )
    return libdir, includedir, version


def main(argv: list[str]) -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument(
        "--libdir",
        type=Path,
        help="Source dylib directory (defaults to Homebrew onnxruntime lib).",
    )
    parser.add_argument(
        "--includedir",
        type=Path,
        help="Source include directory (defaults to Homebrew onnxruntime include).",
    )
    parser.add_argument(
        "--staging",
        type=Path,
        required=True,
        help="Output staging directory.",
    )
    parser.add_argument(
        "--version",
        default=None,
        help="Version string for the generated .pc file.",
    )
    args = parser.parse_args(argv)

    if sys.platform != "darwin":
        sys.stderr.write(
            "stage_macos_onnxruntime.py: only useful on macOS; nothing to do\n"
        )
        return 0

    if args.libdir is None or args.includedir is None or args.version is None:
        try:
            libdir_auto, includedir_auto, version_auto = detect_brew_paths()
        except Exception as exc:
            sys.stderr.write(
                f"stage_macos_onnxruntime.py: --libdir/--includedir/--version "
                f"required (auto-detect failed: {exc})\n"
            )
            return 1
        libdir = args.libdir or libdir_auto
        includedir = args.includedir or includedir_auto
        version = args.version or version_auto
    else:
        libdir = args.libdir
        includedir = args.includedir
        version = args.version

    real_dylib = find_real_dylib(libdir)
    dst = args.staging / "libonnxruntime.1.dylib"
    changed = stage_dylib(real_dylib, dst)
    write_pc(args.staging, includedir, version)
    print(
        f"stage_macos_onnxruntime.py: source={real_dylib}, "
        f"staged={dst} ({'updated' if changed else 'unchanged'}), "
        f"pc={args.staging / 'pkgconfig' / 'libonnxruntime.pc'}"
    )
    # Emit machine-readable lines for meson run_command.
    print(f"PC_DIR={args.staging / 'pkgconfig'}")
    print(f"STAGED_DYLIB={dst}")
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
