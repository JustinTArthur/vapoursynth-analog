#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Convert the bundled ONNX models to native CoreML ``.mlpackage`` bundles (macOS).

macOS wheels ship no ONNX Runtime; the NN decoders run through libchromadec's
native CoreML backend, which loads ``.mlpackage`` bundles. This drives
libchromadec's ``scripts/convert_coreml.py`` for each downloaded ``.onnx`` at its
required fixed input shape (the NN decoders are NTSC-only), then removes the
``.onnx`` so the wheel carries only the CoreML artifacts.

Models are converted at fp32 except nnTransform3D ``chroma_net`` v2, which is
converted at fp16 on Apple silicon so it can run on the Neural Engine (see
``_fp16_safe``).

Run after tools/fetch_models.py. Requires a converter venv:
    pip install coremltools onnx2torch torch onnx

Usage:
    python tools/convert_models_macos.py \
        --convert-script subprojects/chromadec/scripts/convert_coreml.py \
        --python .venv-coreml/bin/python
"""

from __future__ import annotations

import argparse
import platform
import subprocess
import sys
from pathlib import Path

_ROOT = Path(__file__).resolve().parent.parent
_DEFAULT_DEST = _ROOT / "python" / "vsanalog" / "models"
_DEFAULT_SCRIPT = _ROOT / "subprojects" / "chromadec" / "scripts" / "convert_coreml.py"

# Dest-relpath prefixes whose weights survive an fp16 conversion. nnTransform3D
# chroma_net v2 divides its input magnitudes by 128 precisely so the spectrum
# fits fp16's range; the v1 series feeds unscaled magnitudes that overflow it
# and yield NaN masks, and both ldzeug2 models break on fp16 index math. See
# libchromadec's docs/nn-models.md for the measured per-model errors.
_FP16_SAFE_PREFIXES = ("nntransform3d/chroma_net-v2-",)


def _convert_args(rel: str) -> list[str]:
    """convert_coreml.py flags for a model, keyed on its dest-relpath.

    Shapes are the NTSC fixed shapes the CoreML packages are traced at
    (see convert_coreml.py): color_cnn / luma_sep field = 263x910,
    luma_sep frame = 526x910; nnTransform3D ignores height/width.
    """
    if rel.startswith("nntransform3d/"):
        return ["--model-type", "nntransform3d"]
    if rel.startswith("ldzeug/color_cnn_"):
        return ["--model-type", "ldzeug-colorcnn", "--height", "263", "--width", "910"]
    if rel.startswith("ldzeug/luma_sep_2d_frame_"):
        return ["--model-type", "ldzeug-lumasep", "--height", "526", "--width", "910"]
    if rel.startswith("ldzeug/luma_sep_"):
        return ["--model-type", "ldzeug-lumasep", "--height", "263", "--width", "910"]
    raise ValueError(f"no CoreML conversion mapping for {rel!r}")


def _fp16_safe(rel: str, mode: str, machine: str) -> bool:
    """Whether to convert this model at fp16, keyed on its dest-relpath.

    An fp16 program is the only kind the Apple Neural Engine will take, and the
    ANE is the fastest placement for chroma_net v2 — but only where there is
    one. A GPU runs the fp16 kernels at the same rate as the fp32 ones, so the
    package only pays for its pinned fp32 boundary casts and measures slightly
    slower; an Intel Mac has no ANE to redeem that, so ``auto`` converts fp16
    on Apple silicon only.
    """
    if mode == "never" or not rel.startswith(_FP16_SAFE_PREFIXES):
        return False
    return mode == "always" or machine == "arm64"


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("--dest", type=Path, default=_DEFAULT_DEST)
    ap.add_argument("--convert-script", type=Path, default=_DEFAULT_SCRIPT)
    ap.add_argument("--python", default=sys.executable,
                    help="Python interpreter with coremltools/onnx2torch/torch")
    ap.add_argument("--keep-onnx", action="store_true",
                    help="Keep the .onnx files alongside the .mlpackage")
    ap.add_argument("--fp16", default="auto", choices=["auto", "always", "never"],
                    help="Convert the fp16-safe models (nnTransform3D chroma_net "
                         "v2) at fp16, making them Neural Engine eligible: auto "
                         "(default) does so on Apple silicon only, never keeps "
                         "everything fp32")
    args = ap.parse_args()

    onnx_files = sorted(args.dest.rglob("*.onnx"))
    if not onnx_files:
        print(f"no .onnx models under {args.dest}", file=sys.stderr)
        return 1

    for onnx in onnx_files:
        rel = onnx.relative_to(args.dest).as_posix()
        if rel.endswith("-fp16.onnx"):
            # A tools/convert_models_fp16.py sibling for the CUDA/DirectML
            # wheels; the CoreML precision is chosen below from the fp32 file.
            continue
        out = onnx.with_suffix(".mlpackage")
        precision = "fp16" if _fp16_safe(rel, args.fp16, platform.machine()) else "fp32"
        cmd = [
            args.python, str(args.convert_script),
            "--onnx", str(onnx), "--out", str(out),
            "--precision", precision,
            *_convert_args(rel),
        ]
        print("converting:", " ".join(cmd))
        subprocess.run(cmd, check=True)
        if not args.keep_onnx:
            onnx.unlink()

    print("CoreML conversion complete.")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
