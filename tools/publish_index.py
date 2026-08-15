#!/usr/bin/env python3
"""Generate the static PEP 503 index pages for a multi-project wheel site.

The site holds wheels that cannot go to PyPI (too large, or vendor-runtime
specific) for any number of projects, laid out as one Simple-API index per
variant family — a *channel*, in the download.pytorch.org sense — plus one
unified tree for variant-aware installers (PEP 817)::

    <site>/files/<project>/<tag>/…         the artifacts themselves
    <site>/projects.toml                   per-project settings (see below)
    <site>/cu13/index.html                 channel root: every project with a
    <site>/cu13/<project>/index.html       cu13 build: …-0cuda13-…whl
    <site>/cu12/…, <site>/rocm7/…
    <site>/simple/<project>/index.html     variant-labelled wheels
                                           (…-manylinux_2_28_x86_64-cuda13.whl)
                                           + <project>-<ver>-variants.json, and
                                           plain wheels of projects not on PyPI

Channels and simple/ are deliberately disjoint. Legacy pip resolves a project
on a channel against PyPI's plain wheel by build tag (``0cuda13`` beats none),
so a channel holds exactly one build per version and platform. It cannot tell
the labelled wheels apart (it skips their filenames), so they live only under
``simple/`` — where a variant-aware installer picks by hardware — and the
build-tag wheels are kept out of ``simple/`` so a legacy pip pointed there
falls through to PyPI instead of picking a variant by build-tag string order.

``projects.toml`` declares each project::

    [vsanalog]
    pypi = true    # also released on PyPI: plain wheels are refused here,
                   # they would shadow the PyPI release

A project absent from the file is treated as ``pypi = true``. With
``pypi = false`` a project's plain wheels are served from ``simple/`` and the
site is its only index.

Pages are generated from a listing, not from the wheels: each artifact is
described by a ``<name>.sha256`` sidecar (hex digest, ``sha256sum`` format)
written at upload time, so regenerating after a release needs a sync of the
sidecars only, not the multi-GB wheels. A missing sidecar is computed from
the file when it is present locally. An optional ``<wheel>.metadata`` sidecar
(the wheel's METADATA, PEP 658) is linked so resolvers can read extras
without fetching the wheel.

Usage:
    python tools/publish_index.py <site-root> [--files-url URL]
"""

from __future__ import annotations

import argparse
import hashlib
import html
import re
import sys
import tomllib
from pathlib import Path, PurePosixPath

# Channel for each build-tag variant. The build tag is "0<variant>" (digits
# first, per PEP 427) as stamped by CI.
CHANNEL_BY_VARIANT = {
    "cuda12": "cu12",
    "cuda13": "cu13",
    "migraphx": "rocm7",
}
UNIFIED_DIR = "simple"
PROJECTS_FILE = "projects.toml"

# PEP 427 wheel filename with the optional PEP 817 variant label.
_WHEEL_RE = re.compile(
    r"^(?P<name>[^-]+)-(?P<ver>[^-]+)"
    r"(?:-(?P<build>\d[^-]*))?"
    r"-(?P<py>[^-]+)-(?P<abi>[^-]+)-(?P<plat>[^-]+)"
    r"(?:-(?P<label>[0-9a-z._]{1,16}))?"
    r"\.whl$"
)
_VARIANTS_JSON_RE = re.compile(r"^(?P<name>[^-]+)-(?P<ver>[^-]+)-variants\.json$")


def normalize(name: str) -> str:
    return re.sub(r"[-_.]+", "-", name).lower()


class Artifact:
    def __init__(self, project: str, path: Path, digest: str):
        self.project = project
        self.path = path
        self.digest = digest
        self.name = path.name
        self.tree: str | None = None  # channel or UNIFIED_DIR
        self.requires_python: str | None = None
        self.metadata_digest: str | None = None
        self.rel_parts: tuple[str, ...] = ()


def _digest_for(path: Path) -> str | None:
    sidecar = path.with_name(path.name + ".sha256")
    if sidecar.exists():
        return sidecar.read_text(encoding="utf-8").split()[0]
    if path.exists():
        return hashlib.sha256(path.read_bytes()).hexdigest()
    return None


def _requires_python(metadata_text: str) -> str | None:
    for line in metadata_text.splitlines():
        if not line.strip():
            break
        if line.startswith("Requires-Python:"):
            return line.partition(":")[2].strip()
    return None


def load_projects(site: Path) -> dict[str, dict]:
    path = site / PROJECTS_FILE
    if not path.exists():
        return {}
    with path.open("rb") as f:
        return {normalize(k): v for k, v in tomllib.load(f).items()}


def collect(files_dir: Path, project: str, on_pypi: bool = True) -> tuple[list[Artifact], list[str]]:
    """Find every artifact of one project under files_dir/<project> and assign
    it to a tree. Returns the artifacts and problems that must stop publishing.
    """
    artifacts: list[Artifact] = []
    errors: list[str] = []
    seen: set[str] = set()
    per_channel_version: dict[tuple[str, str, str], str] = {}

    for path in sorted((files_dir / project).rglob("*")):
        if path.is_dir():
            continue
        name = path.name
        if name.endswith(".sha256") or name.endswith(".metadata"):
            base = path.with_name(name.rsplit(".", 1)[0])
            if base.exists():
                continue  # handled with the artifact
            path = base  # listing-only artifact: only sidecars synced
            name = base.name
        if name in seen:
            continue
        seen.add(name)

        wheel = _WHEEL_RE.match(name)
        vjson = _VARIANTS_JSON_RE.match(name)
        m = wheel or vjson
        if m is None:
            errors.append(f"{project}/{name}: not a wheel or variants.json")
            continue
        if normalize(m["name"]) != project:
            errors.append(f"{project}/{name}: filed under the wrong project")
            continue

        digest = _digest_for(path)
        if digest is None:
            errors.append(f"{project}/{name}: no file and no .sha256 sidecar")
            continue
        art = Artifact(project, path, digest)

        if vjson:
            art.tree = UNIFIED_DIR
        elif wheel["build"] and wheel["label"]:
            errors.append(f"{project}/{name}: carries both a build tag and a variant label")
            continue
        elif wheel["label"]:
            art.tree = UNIFIED_DIR
        elif wheel["build"]:
            variant = wheel["build"].lstrip("0123456789")
            channel = CHANNEL_BY_VARIANT.get(variant)
            if channel is None:
                errors.append(f"{project}/{name}: unknown build-tag variant {variant!r}")
                continue
            key = (channel, wheel["ver"], wheel["plat"])
            other = per_channel_version.setdefault(key, name)
            if other != name:
                errors.append(f"{project}/{name}: second {channel} wheel for {key[1]}/{key[2]} ({other})")
                continue
            art.tree = channel
        elif on_pypi:
            errors.append(f"{project}/{name}: plain wheel would shadow the PyPI release")
            continue
        else:
            art.tree = UNIFIED_DIR

        if wheel:
            meta = path.with_name(name + ".metadata")
            if meta.exists():
                text = meta.read_bytes()
                art.metadata_digest = hashlib.sha256(text).hexdigest()
                art.requires_python = _requires_python(text.decode("utf-8", "replace"))
        artifacts.append(art)

    return artifacts, errors


def collect_site(site: Path) -> tuple[list[Artifact], list[str]]:
    files_dir = site / "files"
    projects = load_projects(site)
    artifacts: list[Artifact] = []
    errors: list[str] = []
    for entry in sorted(files_dir.iterdir()):
        if not entry.is_dir():
            errors.append(f"files/{entry.name}: artifacts belong under files/<project>/")
            continue
        project = entry.name
        if normalize(project) != project:
            errors.append(f"files/{project}: directory must be the normalized project name")
            continue
        on_pypi = bool(projects.get(project, {}).get("pypi", True))
        arts, errs = collect(files_dir, project, on_pypi)
        artifacts += arts
        errors += errs
    return artifacts, errors


_HEAD = (
    "<!DOCTYPE html>\n<html>\n<head>\n"
    '<meta name="pypi:repository-version" content="1.0">\n'
    "<title>{title}</title>\n</head>\n<body>\n"
)


def _root_page(projects: list[str]) -> str:
    links = "".join(f'<a href="{p}/">{p}</a><br>\n' for p in sorted(projects))
    return _HEAD.format(title="Simple index") + links + "</body>\n</html>\n"


def _project_page(project: str, arts: list[Artifact], files_url: str) -> str:
    lines = [_HEAD.format(title=f"Links for {project}"), f"<h1>Links for {project}</h1>\n"]
    for art in sorted(arts, key=lambda a: a.name):
        rel = PurePosixPath(*art.rel_parts)
        href = f"{files_url}/{rel}#sha256={art.digest}"
        attrs = ""
        if art.requires_python:
            attrs += f' data-requires-python="{html.escape(art.requires_python, quote=True)}"'
        if art.metadata_digest:
            attrs += f' data-core-metadata="sha256={art.metadata_digest}"'
        lines.append(f'<a href="{html.escape(href, quote=True)}"{attrs}>{html.escape(art.name)}</a><br>\n')
    lines.append("</body>\n</html>\n")
    return "".join(lines)


def write_site(site: Path, artifacts: list[Artifact], files_url: str | None) -> list[Path]:
    files_dir = site / "files"
    written: list[Path] = []
    pages: dict[tuple[str, str], list[Artifact]] = {}
    projects = sorted({a.project for a in artifacts})
    for art in artifacts:
        assert art.tree is not None
        art.rel_parts = art.path.relative_to(files_dir).parts
        pages.setdefault((art.tree, art.project), []).append(art)

    # Every project gets a page in every tree, even an empty one, so an
    # existing URL keeps resolving (to "no candidates") rather than 404-ing.
    # Relative hrefs by default so the tree works from any host and locally.
    url = files_url if files_url else "../../files"
    for tree in sorted(set(CHANNEL_BY_VARIANT.values()) | {UNIFIED_DIR}):
        root = site / tree
        root.mkdir(parents=True, exist_ok=True)
        (root / "index.html").write_text(_root_page(projects), encoding="utf-8")
        written.append(root / "index.html")
        for project in projects:
            page_dir = root / project
            page_dir.mkdir(exist_ok=True)
            page = _project_page(project, pages.get((tree, project), []), url)
            (page_dir / "index.html").write_text(page, encoding="utf-8")
            written.append(page_dir / "index.html")
    return written


def main(argv: list[str]) -> int:
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("site", type=Path, help="site root; artifacts under <site>/files/<project>/")
    parser.add_argument(
        "--files-url",
        help="absolute URL prefix for artifacts (default: relative ../../files)",
    )
    args = parser.parse_args(argv[1:])

    site = args.site.resolve()
    if not (site / "files").is_dir():
        sys.exit(f"{site / 'files'}: no such directory")

    artifacts, errors = collect_site(site)
    if errors:
        print("\n".join(errors), file=sys.stderr)
        return 1
    files_url = args.files_url.rstrip("/") if args.files_url else None
    written = write_site(site, artifacts, files_url)

    counts: dict[tuple[str, str], int] = {}
    for art in artifacts:
        key = (art.tree or "?", art.project)
        counts[key] = counts.get(key, 0) + 1
    for (tree, project), n in sorted(counts.items()):
        print(f"{tree}/{project}: {n} artifact(s)")
    print(f"wrote {len(written)} page(s) under {site}")
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
