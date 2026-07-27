#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Convert the bundled ONNX models to native CoreML ``.mlpackage`` bundles (macOS).

macOS wheels ship no ONNX Runtime; the NN decoders run through libchromadec's
native CoreML backend, which loads ``.mlpackage`` bundles. This drives
libchromadec's ``scripts/convert_coreml.py`` for each downloaded ``.onnx`` at its
required fixed input shape (the NN decoders are NTSC-only), then removes the
``.onnx`` so the wheel carries only the CoreML artifacts.

Run after tools/fetch_models.py. Requires a converter venv:
    pip install coremltools onnx2torch torch onnx

Usage:
    python tools/convert_models_macos.py \
        --convert-script subprojects/chromadec/scripts/convert_coreml.py \
        --python .venv-coreml/bin/python
"""

from __future__ import annotations

import argparse
import subprocess
import sys
from pathlib import Path

_ROOT = Path(__file__).resolve().parent.parent
_DEFAULT_DEST = _ROOT / "python" / "vsanalog" / "models"
_DEFAULT_SCRIPT = _ROOT / "subprojects" / "chromadec" / "scripts" / "convert_coreml.py"


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


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("--dest", type=Path, default=_DEFAULT_DEST)
    ap.add_argument("--convert-script", type=Path, default=_DEFAULT_SCRIPT)
    ap.add_argument("--python", default=sys.executable,
                    help="Python interpreter with coremltools/onnx2torch/torch")
    ap.add_argument("--keep-onnx", action="store_true",
                    help="Keep the .onnx files alongside the .mlpackage")
    args = ap.parse_args()

    onnx_files = sorted(args.dest.rglob("*.onnx"))
    if not onnx_files:
        print(f"no .onnx models under {args.dest}", file=sys.stderr)
        return 1

    for onnx in onnx_files:
        rel = onnx.relative_to(args.dest).as_posix()
        out = onnx.with_suffix(".mlpackage")
        cmd = [
            args.python, str(args.convert_script),
            "--onnx", str(onnx), "--out", str(out),
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
