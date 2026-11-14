"""tools/add_variant_extras.py stamps each GPU variant's pip runtime into a
wheel's METADATA. The full unpack/repack round trip needs a built wheel and
runs in CI; here the metadata edit and the requirement sets are checked."""

from __future__ import annotations

import importlib.util
import sys
from pathlib import Path

import pytest


def _load():
    path = Path(__file__).resolve().parent.parent / "tools" / "add_variant_extras.py"
    spec = importlib.util.spec_from_file_location("add_variant_extras", path)
    module = importlib.util.module_from_spec(spec)
    sys.modules[spec.name] = module
    spec.loader.exec_module(module)
    return module


ave = _load()

METADATA = """\
Metadata-Version: 2.1
Name: vsanalog
Version: 1.2.0
Requires-Python: >=3.9
Requires-Dist: vapoursynth>=74

Long description.
"""


def _stamp(tmp_path: Path, variant: str) -> list[str]:
    path = tmp_path / "METADATA"
    path.write_text(METADATA)
    ave._inject(
        path,
        ave.VARIANT_EXTRAS[variant],
        ave.VARIANT_REQUIREMENTS.get(variant, []),
        ave.VARIANT_REQUIRES_PYTHON.get(variant),
    )
    head, _, body = path.read_text().partition("\n\n")
    assert body == "Long description.\n"
    return head.splitlines()


def test_cuda_variants_add_only_extras(tmp_path: Path):
    lines = _stamp(tmp_path, "cuda13")
    plain = [ln for ln in lines if ln.startswith("Requires-Dist:") and "extra ==" not in ln]
    assert plain == ["Requires-Dist: vapoursynth>=74"]
    assert "Provides-Extra: cuda" in lines and "Provides-Extra: tensorrt" in lines


def test_rocm10_requires_plugin_ep_and_runtime(tmp_path: Path):
    lines = _stamp(tmp_path, "migraphx10")
    assert "Requires-Dist: onnxruntime-ep-migraphx==1.0.0+rocm10.1.0" in lines
    assert "Requires-Dist: rocm[libraries]==10.1.0" in lines


def test_rocm10_replaces_requires_python(tmp_path: Path):
    lines = _stamp(tmp_path, "migraphx10")
    assert [ln for ln in lines if ln.startswith("Requires-Python:")] == ["Requires-Python: >=3.11"]


def test_other_variants_keep_requires_python(tmp_path: Path):
    lines = _stamp(tmp_path, "cuda13")
    assert [ln for ln in lines if ln.startswith("Requires-Python:")] == ["Requires-Python: >=3.9"]


def test_rocm10_device_extras_match_gpu_targets(tmp_path: Path):
    lines = _stamp(tmp_path, "migraphx10")
    extras = {ln.split(": ", 1)[1] for ln in lines if ln.startswith("Provides-Extra:")}
    assert extras == {f"device-{t}" for t in ave.ROCM10_GPU_TARGETS} | {"device-all"}
    assert ('Requires-Dist: rocm[libraries,device-gfx1100]==10.1.0 ; '
            'extra == "device-gfx1100"') in lines


def test_rocm10_has_no_gfx906(tmp_path: Path):
    # AMD's pip ROCm 10 has no gfx906 device package; Vega stays on ROCm 7.
    assert "gfx906" not in ave.ROCM10_GPU_TARGETS


def test_refuses_a_wheel_with_extras(tmp_path: Path):
    path = tmp_path / "METADATA"
    path.write_text(METADATA.replace("\n\n", "\nProvides-Extra: x\n\n"))
    with pytest.raises(SystemExit):
        ave._inject(path, ave.VARIANT_EXTRAS["cuda12"])

