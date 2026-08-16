"""Assert an installed vsanalog wheel's plugin autoloads and exposes its API.

tests/test_autoload.py skips when the plugin is absent, which is correct for a
plain source checkout but useless straight after installing a freshly built
wheel: VapourSynth ignores plugins that fail to load, so a wheel whose plugin
cannot resolve its runtime libraries would leave the suite green. This fails.

Run it in an environment where the wheel IS installed (the mirror image of
tools/check_standalone_plugin.py, which needs the wheel absent).

    python tools/check_wheel_plugin.py

Set ``VSANALOG_EXPECT_CPU_TIERS`` (comma-separated, e.g. ``avx2``) to also
assert the CPU-optimized variants the build was supposed to inject are there.
"""

from __future__ import annotations

import os
import platform
import sys
import sysconfig
from pathlib import Path

import vapoursynth as vs

EXPECTED_FUNCTIONS = (
    "amplify_chroma", "create_dropouts_mask", "decode_4fsc_video",
    "modernize_chromaticity", "set_log_level",
)


def check_cpu_tiers() -> None:
    """Assert the wheel carries the CPU variants the build meant to inject.

    VapourSynth finds them by probing for siblings of the manifest entry and
    quietly settles for the baseline when there are none, so a build that
    stopped adding them is indistinguishable at runtime from one that never
    wanted them — which is how the GPU wheels shipped baseline-only.
    """
    expected = [
        t.strip()
        for t in os.environ.get("VSANALOG_EXPECT_CPU_TIERS", "").split(",")
        if t.strip()
    ]
    if not expected:
        return

    suffix = {"Windows": ".dll", "Darwin": ".dylib"}.get(platform.system(), ".so")
    plugin_dir = (
        Path(sysconfig.get_paths()["purelib"]) / "vapoursynth" / "plugins" / "vsanalog"
    )
    missing = [t for t in expected if not (plugin_dir / f"vsanalog.{t}{suffix}").is_file()]
    if missing:
        sys.exit(
            f"the installed wheel is missing CPU variant(s) {', '.join(missing)} "
            f"in {plugin_dir}. Every x86 wheel job builds them at a wider ISA and "
            "injects them with tools/add_cpu_tiers.py before the repair step."
        )
    print(f"CPU variant(s) present beside the plugin: {', '.join(expected)}")


def main() -> None:
    if not hasattr(vs.core, "analog"):
        sys.exit(
            "the installed wheel's plugin did not autoload. VapourSynth skips "
            "plugins it cannot load, so this usually means an unresolved runtime "
            "library — on a GPU wheel, the vendor runtime it links against."
        )

    missing = [f for f in EXPECTED_FUNCTIONS if not hasattr(vs.core.analog, f)]
    if missing:
        sys.exit(f"plugin autoloaded but is missing: {', '.join(missing)}")

    from vsanalog import plugin

    plugin._ensure_plugin_loaded()

    check_cpu_tiers()

    print(
        "installed wheel's plugin autoloaded; analog namespace provides "
        + ", ".join(sorted(f.name for f in vs.core.analog.functions()))
    )


if __name__ == "__main__":
    main()
