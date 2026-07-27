#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Download and checksum-verify the bundled NN models listed in models.lock.

Reads ``tools/models.lock`` and downloads each model into
``python/vsanalog/models/<dest-relpath>``, verifying its SHA-256. Existing files
with the correct hash are left in place, so re-runs are cheap and offline-safe.

Usage:
    python tools/fetch_models.py [--dest DIR] [--lock FILE]
"""

from __future__ import annotations

import argparse
import hashlib
import sys
import urllib.request
from pathlib import Path

_ROOT = Path(__file__).resolve().parent.parent
_DEFAULT_LOCK = _ROOT / "tools" / "models.lock"
_DEFAULT_DEST = _ROOT / "python" / "vsanalog" / "models"


def _parse_lock(lock_path: Path) -> list[tuple[str, str, str]]:
    entries = []
    for raw in lock_path.read_text().splitlines():
        line = raw.strip()
        if not line or line.startswith("#"):
            continue
        parts = line.split()
        if len(parts) != 3:
            raise ValueError(f"malformed models.lock line: {raw!r}")
        rel, sha256, url = parts
        entries.append((rel, sha256.lower(), url))
    return entries


def _sha256(path: Path) -> str:
    h = hashlib.sha256()
    with path.open("rb") as f:
        for chunk in iter(lambda: f.read(1 << 20), b""):
            h.update(chunk)
    return h.hexdigest()


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("--lock", type=Path, default=_DEFAULT_LOCK)
    ap.add_argument("--dest", type=Path, default=_DEFAULT_DEST)
    args = ap.parse_args()

    entries = _parse_lock(args.lock)
    print(f"{len(entries)} model(s) in {args.lock}")

    for rel, expected, url in entries:
        out = args.dest / rel
        out.parent.mkdir(parents=True, exist_ok=True)

        if out.is_file() and _sha256(out) == expected:
            print(f"  ok (cached)  {rel}")
            continue

        print(f"  downloading  {rel}  <- {url}")
        tmp = out.with_suffix(out.suffix + ".part")
        urllib.request.urlretrieve(url, tmp)  # noqa: S310 (trusted host)
        actual = _sha256(tmp)
        if actual != expected:
            tmp.unlink(missing_ok=True)
            print(
                f"  CHECKSUM MISMATCH {rel}\n"
                f"    expected {expected}\n    actual   {actual}",
                file=sys.stderr,
            )
            return 1
        tmp.replace(out)
        print(f"  verified     {rel}")

    print("all models present and verified.")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
