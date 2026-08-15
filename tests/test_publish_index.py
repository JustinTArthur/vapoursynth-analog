"""tools/publish_index.py sorts each project's wheels into disjoint index trees.

The channels (cu12/cu13/rocm7) hold only build-tag wheels, one per version
and platform; simple/ holds variant-labelled wheels, variants.json, and plain
wheels of projects that are not on PyPI. Mixing them would let a legacy pip
choose a variant by build-tag string order, or shadow a PyPI release.
"""

from __future__ import annotations

import hashlib
import importlib.util
import sys
from pathlib import Path

import pytest

_ROOT = Path(__file__).resolve().parent.parent


def _load():
    path = _ROOT / "tools" / "publish_index.py"
    spec = importlib.util.spec_from_file_location("publish_index", path)
    module = importlib.util.module_from_spec(spec)
    sys.modules[spec.name] = module
    spec.loader.exec_module(module)
    return module


pi = _load()

V = "vsanalog-1.2.0"
LINUX = "py3-none-manylinux_2_28_x86_64"
WIN = "py3-none-win_amd64"


def _touch(files: Path, *names: str, sidecar_only: bool = False) -> None:
    files.mkdir(parents=True, exist_ok=True)
    for name in names:
        if sidecar_only:
            (files / (name + ".sha256")).write_text("ab" * 32 + f"  {name}\n")
        else:
            (files / name).write_bytes(name.encode())


def _trees(site: Path) -> dict[str, list[str]]:
    arts, errors = pi.collect_site(site)
    assert not errors, errors
    out: dict[str, list[str]] = {}
    for a in arts:
        out.setdefault(f"{a.tree}/{a.project}", []).append(a.name)
    return {k: sorted(v) for k, v in out.items()}


def test_trees_are_disjoint(tmp_path: Path):
    files = tmp_path / "files" / "vsanalog" / "v1.2.0"
    _touch(
        files,
        f"{V}-0cuda12-{LINUX}.whl",
        f"{V}-0cuda12-{WIN}.whl",
        f"{V}-0cuda13-{LINUX}.whl",
        f"{V}-0migraphx-{LINUX}.whl",
        f"{V}-{LINUX}-cuda12.whl",
        f"{V}-{LINUX}-cuda13.whl",
        f"{V}-variants.json",
    )
    assert _trees(tmp_path) == {
        "cu12/vsanalog": [f"{V}-0cuda12-{LINUX}.whl", f"{V}-0cuda12-{WIN}.whl"],
        "cu13/vsanalog": [f"{V}-0cuda13-{LINUX}.whl"],
        "rocm7/vsanalog": [f"{V}-0migraphx-{LINUX}.whl"],
        "simple/vsanalog": [f"{V}-{LINUX}-cuda12.whl", f"{V}-{LINUX}-cuda13.whl", f"{V}-variants.json"],
    }


def test_multiple_projects_and_pypi_flag(tmp_path: Path):
    _touch(tmp_path / "files" / "vsanalog" / "v1.2.0", f"{V}-0cuda13-{LINUX}.whl")
    _touch(tmp_path / "files" / "other-tool" / "v0.1", "other_tool-0.1-py3-none-any.whl")
    (tmp_path / "projects.toml").write_text("[other-tool]\npypi = false\n")
    assert _trees(tmp_path) == {
        "cu13/vsanalog": [f"{V}-0cuda13-{LINUX}.whl"],
        "simple/other-tool": ["other_tool-0.1-py3-none-any.whl"],
    }
    # Every project gets a page in every tree; channel roots list them all.
    arts, _ = pi.collect_site(tmp_path)
    pi.write_site(tmp_path, arts, None)
    for tree in ("cu12", "cu13", "rocm7", "simple"):
        root = (tmp_path / tree / "index.html").read_text()
        assert 'href="vsanalog/"' in root and 'href="other-tool/"' in root
        assert (tmp_path / tree / "other-tool" / "index.html").exists()
    cu13_other = (tmp_path / "cu13" / "other-tool" / "index.html").read_text()
    assert "<a href=" not in cu13_other


@pytest.mark.parametrize(
    "name",
    [
        f"{V}-{LINUX}.whl",  # plain: would shadow the PyPI wheel
        f"{V}-0cuda13-{LINUX}-cuda13.whl",  # both build tag and label
        f"{V}-0rocm-{LINUX}.whl",  # unknown variant
        f"other-1.0-0cuda13-{LINUX}.whl",  # filed under the wrong project
        "notes.txt",
    ],
)
def test_rejects_misfiled_artifacts(tmp_path: Path, name: str):
    _touch(tmp_path / "files" / "vsanalog", name)
    _, errors = pi.collect_site(tmp_path)
    assert errors and name in errors[0]


def test_rejects_stray_files_and_unnormalized_dirs(tmp_path: Path):
    _touch(tmp_path / "files", f"{V}-0cuda13-{LINUX}.whl")
    _touch(tmp_path / "files" / "VS_Analog", f"{V}-0cuda13-{LINUX}.whl")
    _, errors = pi.collect_site(tmp_path)
    assert len(errors) == 2
    joined = "\n".join(errors)
    assert "files/<project>/" in joined and "normalized" in joined


def test_rejects_two_builds_per_channel_version(tmp_path: Path):
    files = tmp_path / "files" / "vsanalog"
    _touch(files / "a", f"{V}-0cuda13-{LINUX}.whl")
    _touch(files / "b", f"{V}-1cuda13-{LINUX}.whl")
    _, errors = pi.collect_site(tmp_path)
    assert errors and "second cu13 wheel" in errors[0]


def test_listing_only_from_sidecars(tmp_path: Path):
    _touch(tmp_path / "files" / "vsanalog" / "v1.2.0", f"{V}-0cuda13-{LINUX}.whl", sidecar_only=True)
    arts, errors = pi.collect_site(tmp_path)
    assert not errors
    assert [a.digest for a in arts] == ["ab" * 32]


def test_pages_link_hashes_and_metadata(tmp_path: Path):
    files = tmp_path / "files" / "vsanalog" / "v1.2.0"
    wheel = f"{V}-0cuda13-{LINUX}.whl"
    _touch(files, wheel)
    meta = b"Metadata-Version: 2.1\nName: vsanalog\nRequires-Python: >=3.9\n\n"
    (files / (wheel + ".metadata")).write_bytes(meta)
    arts, errors = pi.collect_site(tmp_path)
    assert not errors
    pi.write_site(tmp_path, arts, None)

    page = (tmp_path / "cu13" / "vsanalog" / "index.html").read_text()
    digest = hashlib.sha256(wheel.encode()).hexdigest()
    assert f'href="../../files/vsanalog/v1.2.0/{wheel}#sha256={digest}"' in page
    assert f'data-core-metadata="sha256={hashlib.sha256(meta).hexdigest()}"' in page
    assert 'data-requires-python="&gt;=3.9"' in page

    pi.write_site(tmp_path, arts, "https://py.example.com/files")
    page = (tmp_path / "cu13" / "vsanalog" / "index.html").read_text()
    assert f'href="https://py.example.com/files/vsanalog/v1.2.0/{wheel}#sha256=' in page
