"""Load a standalone plugin build and confirm it registers its functions.

Used by CI on the contents of an extracted release tarball. Run it in an
environment where the vsanalog wheel is *not* installed: the wheel's plugin
autoloads from site-packages and would claim the same plugin ID, masking
whatever the tarball copy does.

    python tools/check_standalone_plugin.py <directory-or-plugin-path>
"""

from __future__ import annotations

import sys
from pathlib import Path

import vapoursynth as vs

PLUGIN_NAMES = ("vsanalog.so", "vsanalog.dylib", "vsanalog.dll")
EXPECTED_FUNCTIONS = ("decode_4fsc_video", "set_log_level")


def find_plugin(target: Path) -> Path:
    if target.is_file():
        return target
    for candidate in sorted(target.rglob("*")):
        if candidate.name in PLUGIN_NAMES:
            return candidate
    sys.exit(f"no {' / '.join(PLUGIN_NAMES)} found under {target}")


def main() -> None:
    if len(sys.argv) != 2:
        sys.exit(__doc__)

    plugin = find_plugin(Path(sys.argv[1]))
    core = vs.core
    core.std.LoadPlugin(str(plugin.resolve()))

    missing = [f for f in EXPECTED_FUNCTIONS if not hasattr(core.analog, f)]
    if missing:
        sys.exit(f"{plugin} loaded but is missing: {', '.join(missing)}")

    print(f"{plugin} loaded; analog namespace provides "
          f"{', '.join(sorted(f.name for f in core.analog.functions()))}")


if __name__ == "__main__":
    main()
