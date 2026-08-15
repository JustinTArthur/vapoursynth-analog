#!/usr/bin/env python3
"""Stage the GPU wheels of one release for the package index.

Takes the build-tag wheels CI produced (``…-0cuda13-…whl`` etc.) and fills a
``files/<project>/<tag>/`` directory with everything tools/publish_index.py
lists:

- the wheels themselves (channel installs by legacy pip);
- for each CUDA wheel, a PEP 817 variant-labelled twin without the build tag
  (``…-manylinux_2_28_x86_64-cuda13.whl``) made by ``variantlib
  make-variant``, plus the ``<project>-<ver>-variants.json`` index file
  (variant-aware installs from simple/);
- ``.sha256`` sidecars for every artifact and ``.metadata`` sidecars (PEP 658)
  for every wheel, so the index can be regenerated from a listing.

The labelled twins are best-effort: variant tooling is still pre-standard,
so a failure there is reported as a warning and the channel wheels ship
regardless. Requires ``wheel`` and, for the twins, ``variantlib`` (plus
``build``, which it imports without declaring) and the NVIDIA provider.

Usage:
    python tools/stage_gpu_wheels.py <wheel-dir> <out-dir> [--pyproject pyproject.toml]
"""

from __future__ import annotations

import argparse
import hashlib
import re
import shutil
import subprocess
import sys
import tempfile
import zipfile
from pathlib import Path

# PEP 817 properties per build-tag variant. The NVIDIA provider reports the
# driver's CUDA version; the wheels need a runtime of that major or newer.
# MIGraphX has no provider yet, so no twin is made for it.
VARIANT_PROPERTIES = {
    "cuda12": ["nvidia :: cuda_version_lower_bound :: 12"],
    "cuda13": ["nvidia :: cuda_version_lower_bound :: 13"],
}

_BUILD_TAG_RE = re.compile(r"^[^-]+-[^-]+-(?P<build>\d[^-]*)-[^-]+-[^-]+-[^-]+\.whl$")


def _run(cmd: list[str], **kw) -> subprocess.CompletedProcess:
    print("+", " ".join(cmd), flush=True)
    return subprocess.run(cmd, check=True, text=True, **kw)


def _warn(msg: str) -> None:
    # GitHub Actions renders ::warning:: lines in the job summary.
    print(f"::warning::{msg}", flush=True)
    print(f"warning: {msg}", file=sys.stderr, flush=True)


def make_variant_twin(wheel: Path, variant: str, out_dir: Path, pyproject: Path) -> Path | None:
    props = VARIANT_PROPERTIES.get(variant)
    if props is None:
        return None
    with tempfile.TemporaryDirectory() as td:
        tmp = Path(td)
        # Strip the build tag on a copy: a labelled wheel must not carry one.
        shutil.copy(wheel, tmp / wheel.name)
        _run([sys.executable, "-m", "wheel", "tags", "--build", "", "--remove", str(tmp / wheel.name)], cwd=tmp)
        (untagged,) = tmp.glob("*.whl")
        base = [
            "variantlib", "make-variant", "-f", str(untagged), "-o", str(out_dir),
            "--no-isolation", "--pyproject-toml", str(pyproject), "--variant-label", variant,
            "-p", *props,
        ]
        try:
            _run(base)
        except (subprocess.CalledProcessError, FileNotFoundError) as exc:
            # The provider plugin only loads on Linux/Windows with the plugin
            # API variantlib expects; the properties above are known-good.
            _warn(f"{wheel.name}: make-variant with plugin validation failed ({exc}); retrying without")
            _run(base + ["--skip-plugin-validation"])
    twins = [p for p in out_dir.glob(f"{untagged.stem}-{variant}.whl")]
    return twins[0] if twins else None


def write_sidecars(out_dir: Path) -> None:
    for path in sorted(out_dir.iterdir()):
        if path.suffix == ".whl" or path.name.endswith("-variants.json"):
            digest = hashlib.sha256(path.read_bytes()).hexdigest()
            (out_dir / (path.name + ".sha256")).write_text(f"{digest}  {path.name}\n")
        if path.suffix == ".whl":
            with zipfile.ZipFile(path) as zf:
                (name,) = [n for n in zf.namelist() if n.endswith(".dist-info/METADATA")]
                (out_dir / (path.name + ".metadata")).write_bytes(zf.read(name))


def main(argv: list[str]) -> int:
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("wheel_dir", type=Path)
    parser.add_argument("out_dir", type=Path)
    parser.add_argument("--pyproject", type=Path, default=Path("pyproject.toml"))
    parser.add_argument("--no-variants", action="store_true", help="skip the PEP 817 twins")
    args = parser.parse_args(argv[1:])

    wheels = sorted(args.wheel_dir.glob("*.whl"))
    if not wheels:
        sys.exit(f"{args.wheel_dir}: no wheels")
    args.out_dir.mkdir(parents=True, exist_ok=True)

    twins = 0
    for wheel in wheels:
        m = _BUILD_TAG_RE.match(wheel.name)
        if m is None:
            sys.exit(f"{wheel.name}: GPU wheels must carry a build tag")
        shutil.copy(wheel, args.out_dir / wheel.name)
        if args.no_variants:
            continue
        variant = m["build"].lstrip("0123456789")
        try:
            if make_variant_twin(wheel, variant, args.out_dir, args.pyproject.resolve()) is not None:
                twins += 1
        except (subprocess.CalledProcessError, FileNotFoundError) as exc:
            _warn(f"{wheel.name}: no variant twin ({exc})")

    if twins:
        try:
            _run(["variantlib", "generate-index-json", "-d", str(args.out_dir)])
        except (subprocess.CalledProcessError, FileNotFoundError) as exc:
            _warn(f"variants.json not generated ({exc}); removing labelled twins")
            for p in args.out_dir.glob("*.whl"):
                if _BUILD_TAG_RE.match(p.name) is None:
                    p.unlink()
            twins = 0

    write_sidecars(args.out_dir)
    staged = sorted(p.name for p in args.out_dir.iterdir())
    print(f"staged {len(wheels)} wheel(s), {twins} variant twin(s) in {args.out_dir}:")
    print("\n".join(f"  {n}" for n in staged))
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
