#!/usr/bin/env python3
"""Reconstitute Qt .framework directory structure in a delocated macOS wheel.

delocate-wheel copies a Qt framework's inner Mach-O binary as a flat file
into ``vsanalog/.dylibs/`` and rewrites dependents to point at the flat
path. That loses the surrounding ``QtCore.framework/Versions/A/Resources/
Info.plist``, which is what Qt's own ``QLibraryInfoPrivate::paths`` uses
to find QtCore via ``CFBundleGetBundleWithIdentifier(org.qt-project.<Name>)``.
With the framework gone, Qt falls back to ``CFBundleGetMainBundle`` →
``CFBundleCopyBundleURL`` — which faults on Apple Silicon macOS 14+'s PAC
check in any dlopen-from-LoadPlugin process (the smoke-test layout).

This script reverses the flattening: for each bundled file whose filename
looks like a Qt framework binary (``Qt<Name>`` with no extension), it
relocates the binary into a real framework layout, writes a minimal
``Info.plist`` with the matching ``CFBundleIdentifier``, restores the
standard symlinks, and rewrites every dependent binary in the wheel to
the framework path. Affected binaries get re-signed ad-hoc so dyld will
load them on Apple Silicon.

macOS-only. Linux/Windows don't ship Qt as frameworks.
"""
from __future__ import annotations

import os
import plistlib
import re
import shutil
import subprocess
import sys
import tempfile
from pathlib import Path

# Matches a Qt framework binary's filename (e.g. ``QtCore``, ``QtGui``).
_QT_FRAMEWORK_NAME = re.compile(r"^Qt[A-Z]\w*$")

# Extracts the "Qt 6.x.y" version banner that Qt embeds in QtCore at link time.
_QT_VERSION_BANNER = re.compile(rb"Qt (\d+\.\d+(?:\.\d+)?) ")


def _qt_version(binary: Path) -> str:
    data = binary.read_bytes()
    match = _QT_VERSION_BANNER.search(data)
    return match.group(1).decode() if match else "6.0.0"


def _otool_deps(binary: Path) -> list[str]:
    out = subprocess.check_output(["otool", "-L", str(binary)], text=True)
    deps: list[str] = []
    for line in out.splitlines()[1:]:
        line = line.strip()
        if not line:
            continue
        deps.append(line.split(" ", 1)[0])
    return deps


def _restore_framework(flat: Path, qt_version: str) -> Path:
    """Move ``flat`` into a minimal framework layout CFBundle can resolve.

    Mirrors PyQt6's wheel-friendly variant: ``Resources/Info.plist`` at the
    framework root and the binary under ``Versions/A/``. No symlinks — wheels
    don't preserve them well, and CFBundle's lookup checks both locations.
    """
    name = flat.name
    fwk = flat.with_name(f"{name}.framework")
    versions_a = fwk / "Versions" / "A"
    resources = fwk / "Resources"
    versions_a.mkdir(parents=True, exist_ok=False)
    resources.mkdir(parents=True, exist_ok=False)

    binary_in_fwk = versions_a / name
    shutil.move(str(flat), str(binary_in_fwk))

    plist = {
        "CFBundleIdentifier": f"org.qt-project.{name}",
        "CFBundleExecutable": name,
        "CFBundlePackageType": "FMWK",
        "CFBundleName": name,
        "CFBundleVersion": qt_version,
        "CFBundleShortVersionString": qt_version,
    }
    with (resources / "Info.plist").open("wb") as fh:
        plistlib.dump(plist, fh)

    subprocess.run(
        ["install_name_tool", "-id",
         f"@rpath/{name}.framework/Versions/A/{name}", str(binary_in_fwk)],
        check=True,
    )
    return binary_in_fwk


def _rewrite_deps(binary: Path, rewrites: dict[str, str]) -> bool:
    deps = _otool_deps(binary)
    args: list[str] = []
    for dep in deps:
        new = rewrites.get(dep)
        if new and new != dep:
            args.extend(["-change", dep, new])
    if not args:
        return False
    subprocess.run(["install_name_tool", *args, str(binary)], check=True)
    return True


def _resign(binary: Path) -> None:
    subprocess.run(
        ["codesign", "--force", "--sign", "-", str(binary)],
        check=True, stderr=subprocess.DEVNULL,
    )


def _walk_macho(root: Path):
    for dirpath, _, filenames in os.walk(root, followlinks=False):
        for fname in filenames:
            p = Path(dirpath) / fname
            if p.is_symlink():
                continue
            try:
                with p.open("rb") as fh:
                    magic = fh.read(4)
            except OSError:
                continue
            # Mach-O magic numbers (32/64-bit + fat).
            if magic in (b"\xcf\xfa\xed\xfe", b"\xfe\xed\xfa\xcf",
                         b"\xce\xfa\xed\xfe", b"\xfe\xed\xfa\xce",
                         b"\xca\xfe\xba\xbe", b"\xbe\xba\xfe\xca"):
                yield p


def main(argv: list[str]) -> int:
    if len(argv) != 3:
        sys.stderr.write(f"usage: {argv[0]} <wheel> <output_dir>\n")
        return 2

    wheel = Path(argv[1]).resolve()
    out_dir = Path(argv[2]).resolve()
    if not wheel.is_file():
        sys.stderr.write(f"wheel not found: {wheel}\n")
        return 1
    out_dir.mkdir(parents=True, exist_ok=True)

    with tempfile.TemporaryDirectory() as td:
        tmp = Path(td)
        subprocess.run(
            [sys.executable, "-m", "wheel", "unpack", "-d", str(tmp), str(wheel)],
            check=True,
        )
        unpacked = next(p for p in tmp.iterdir() if p.is_dir())

        dylibs_dir = unpacked / "vsanalog" / ".dylibs"
        if not dylibs_dir.is_dir():
            sys.stderr.write("no vsanalog/.dylibs/ in wheel — nothing to do\n")
            return 0

        # Find flat Qt framework binaries (filename like "Qt<Name>", no extension)
        flat_qt = [
            p for p in dylibs_dir.iterdir()
            if p.is_file() and not p.is_symlink() and _QT_FRAMEWORK_NAME.match(p.name)
        ]
        if not flat_qt:
            print("no flat Qt framework binaries found — nothing to do")
            return 0

        # rewrites maps every old @loader_path/.../<Name> reference (relative to
        # any depth) to the new framework-qualified path. We build it as a set
        # of (suffix_without_dirs → name) and key by full path per binary.
        rewrites: dict[str, str] = {}
        for flat in flat_qt:
            qt_version = _qt_version(flat)
            new_binary = _restore_framework(flat, qt_version)
            print(f"restored {flat.name} → {new_binary.relative_to(unpacked)} "
                  f"(Qt {qt_version})")
            # Dependents reference the old flat path. The "leaf" is what
            # changes; the loader_path prefix differs by depth. Match by suffix.
            rewrites[flat.name] = new_binary.name  # placeholder; real match below

        # Rewrite every Mach-O in the unpacked tree whose deps contain the
        # old flat path. We match by the deps endswith the bare filename and
        # not already inside the framework.
        flat_names = {p.name for p in flat_qt}
        for binary in _walk_macho(unpacked):
            deps = _otool_deps(binary)
            per_binary: dict[str, str] = {}
            for dep in deps:
                leaf = dep.rsplit("/", 1)[-1]
                if leaf in flat_names and ".framework/" not in dep:
                    new = dep.rsplit("/", 1)[0] + f"/{leaf}.framework/Versions/A/{leaf}"
                    per_binary[dep] = new
            if per_binary and _rewrite_deps(binary, per_binary):
                _resign(binary)
                print(f"rewrote deps in {binary.relative_to(unpacked)}: "
                      + ", ".join(f"{k.rsplit('/',1)[-1]}→framework" for k in per_binary))

        # Sign every framework binary too (we changed its install_name).
        for flat in flat_qt:
            fwk_binary = dylibs_dir / f"{flat.name}.framework" / "Versions" / "A" / flat.name
            _resign(fwk_binary)

        wheel.unlink()
        subprocess.run(
            [sys.executable, "-m", "wheel", "pack", "-d", str(out_dir), str(unpacked)],
            check=True,
        )

    print(f"restored Qt framework(s) in {wheel.name}")
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))