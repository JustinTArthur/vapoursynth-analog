"""Assert an installed vsanalog wheel's plugin autoloads and exposes its API.

tests/test_autoload.py skips when the plugin is absent, which is correct for a
plain source checkout but useless straight after installing a freshly built
wheel: VapourSynth ignores plugins that fail to load, so a wheel whose plugin
cannot resolve its runtime libraries would leave the suite green. This fails.

Run it in an environment where the wheel IS installed (the mirror image of
tools/check_standalone_plugin.py, which needs the wheel absent).

    python tools/check_wheel_plugin.py
"""

from __future__ import annotations

import sys

import vapoursynth as vs

EXPECTED_FUNCTIONS = ("decode_4fsc_video", "set_log_level")


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

    import vsanalog

    vsanalog._ensure_plugin_loaded()

    print(
        "installed wheel's plugin autoloaded; analog namespace provides "
        + ", ".join(sorted(f.name for f in vs.core.analog.functions()))
    )


if __name__ == "__main__":
    main()
