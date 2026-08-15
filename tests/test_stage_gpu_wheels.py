"""tools/stage_gpu_wheels.py copies build-tag wheels and writes the sidecars
tools/publish_index.py lists from. The PEP 817 twins need variantlib and are
exercised in CI only; here the --no-variants path is checked."""

from __future__ import annotations

import hashlib
import importlib.util
import sys
import zipfile
from pathlib import Path

import pytest

_ROOT = Path(__file__).resolve().parent.parent


def _load():
    path = _ROOT / "tools" / "stage_gpu_wheels.py"
    spec = importlib.util.spec_from_file_location("stage_gpu_wheels", path)
    module = importlib.util.module_from_spec(spec)
    sys.modules[spec.name] = module
    spec.loader.exec_module(module)
    return module


stage = _load()
META = b"Metadata-Version: 2.1\nName: vsanalog\nVersion: 1.2.0\n\n"


def _wheel(path: Path) -> Path:
    with zipfile.ZipFile(path, "w") as zf:
        zf.writestr("vsanalog-1.2.0.dist-info/METADATA", META)
        zf.writestr("vsanalog-1.2.0.dist-info/WHEEL", "Wheel-Version: 1.0\n")
    return path


def test_stages_wheels_with_sidecars(tmp_path: Path):
    src = tmp_path / "in"
    src.mkdir()
    name = "vsanalog-1.2.0-0cuda13-py3-none-manylinux_2_28_x86_64.whl"
    _wheel(src / name)
    out = tmp_path / "files" / "vsanalog" / "v1.2.0"
    assert stage.main(["stage", str(src), str(out), "--no-variants"]) == 0
    assert sorted(p.name for p in out.iterdir()) == [name, name + ".metadata", name + ".sha256"]
    digest = hashlib.sha256((out / name).read_bytes()).hexdigest()
    assert (out / (name + ".sha256")).read_text() == f"{digest}  {name}\n"
    assert (out / (name + ".metadata")).read_bytes() == META


def test_refuses_untagged_wheel(tmp_path: Path):
    src = tmp_path / "in"
    src.mkdir()
    _wheel(src / "vsanalog-1.2.0-py3-none-manylinux_2_28_x86_64.whl")
    with pytest.raises(SystemExit, match="build tag"):
        stage.main(["stage", str(src), str(tmp_path / "out"), "--no-variants"])
