#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Emit fp16 ``.onnx`` siblings for the fp16-safe bundled models.

CUDA and DirectML have no engine-level fp16 mode the way TensorRT
(trt_fp16_enable) and MIGraphX (migraphx_fp16_enable) do — they just run
whatever dtype the graph declares. Their only route to fp16 is a graph
converted to fp16 ahead of time, which this produces as a
``<stem>-fp16.onnx`` sibling next to the fp32 original; ``vsanalog``'s
``_maybe_fp16_sibling`` picks it up for an explicit ``cuda``/``directml``
provider pin. Unlike tools/convert_models_macos.py, the fp32 original is
kept: TensorRT, MIGraphX, CPU, and an unpinned "auto" request all still need
it.

Same fp16-safety gating as convert_models_macos.py: only nnTransform3D
chroma_net v2 divides its input magnitudes by 128 to keep the spectrum
inside fp16 range. Every other bundled model overflows it or breaks on fp16
index math (see libchromadec's docs/nn-models.md).

Run after tools/fetch_models.py. Requires the pip `onnxruntime` and `onnx`
packages (only onnxruntime's bundled onnxruntime.transformers.float16
converter is used here, and it works on `onnx` model protos; no ONNX Runtime
session is created, so a plain CPU wheel is enough):
    pip install "onnxruntime<2" "onnx<2"

Usage:
    python tools/convert_models_fp16.py
"""

from __future__ import annotations

import argparse
import sys
from pathlib import Path

_ROOT = Path(__file__).resolve().parent.parent
_DEFAULT_DEST = _ROOT / "python" / "vsanalog" / "models"

# Dest-relpath prefixes whose weights survive an fp16 conversion. Kept in
# sync with tools/convert_models_macos.py's _FP16_SAFE_PREFIXES by hand —
# both encode the same fp16-safety fact about the same bundled models.
_FP16_SAFE_PREFIXES = ("nntransform3d/chroma_net-v2-",)


_FP16_SUFFIX = "-fp16"


def _fp16_sibling(onnx: Path) -> Path:
    return onnx.with_name(f"{onnx.stem}{_FP16_SUFFIX}{onnx.suffix}")


def _is_fp16_safe(rel: str) -> bool:
    """Whether the dest-relpath names an fp32 model that converts safely.

    An already-converted sibling matches the prefix too, so it is excluded by
    its suffix; otherwise a second run would emit ``-fp16-fp16.onnx``.
    """
    return rel.startswith(_FP16_SAFE_PREFIXES) and not rel.endswith(f"{_FP16_SUFFIX}.onnx")


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("--dest", type=Path, default=_DEFAULT_DEST)
    args = ap.parse_args()

    import onnx
    from onnxruntime.transformers.float16 import convert_float_to_float16
    from onnxruntime.transformers.onnx_model import OnnxModel

    onnx_files = sorted(args.dest.rglob("*.onnx"))
    fp16_safe = [f for f in onnx_files if _is_fp16_safe(f.relative_to(args.dest).as_posix())]
    if not fp16_safe:
        print(f"no fp16-safe .onnx models under {args.dest}", file=sys.stderr)
        return 1

    for onnx_path in fp16_safe:
        out = _fp16_sibling(onnx_path)
        print(f"converting: {onnx_path} -> {out}")
        model = onnx.load(str(onnx_path))
        # keep_io_types pins the graph's inputs/outputs to fp32 with Casts
        # inside; the CUDA/DirectML pipelines feed and read fp32 buffers.
        fp16_model = convert_float_to_float16(model, keep_io_types=True)
        # convert_float_to_float16 appends its Cast nodes at the end of the
        # node list rather than in topological order, which the ONNX spec
        # requires; OnnxModel.save_model_to_file sorts before writing, same
        # as onnxruntime's own transformer tooling does after this exact
        # conversion.
        OnnxModel(fp16_model).save_model_to_file(str(out), use_external_data_format=False)
        onnx.checker.check_model(str(out))

    print("fp16 conversion complete.")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
