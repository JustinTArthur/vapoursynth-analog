"""Locating and loading the bundled vsanalog VapourSynth plugin.

Kept out of the package root so feature modules can depend on it without
importing a half-initialised ``vsanalog``.
"""

from __future__ import annotations

import functools
import platform
import sys
from collections.abc import Callable
from pathlib import Path
from typing import TypeVar

if sys.version_info >= (3, 10):
    from typing import ParamSpec
else:
    from typing_extensions import ParamSpec

import vapoursynth as vs

__all__ = ["requires_plugin"]

P = ParamSpec("P")
R = TypeVar("R")


def _get_plugin_path() -> Path:
    """Derive the filesystem path of the bundled vsanalog shared library.

    The plugin lives in its own subdirectory of ``vapoursynth/plugins`` so that
    its bundled dependencies can sit beside it; ``manifest.vs`` keeps the
    autoloader from probing those as plugins.
    """
    suffix = {"Windows": ".dll", "Darwin": ".dylib"}.get(platform.system(), ".so")
    _packages_root = Path(__file__).resolve().parent.parent
    return (
        _packages_root / "vapoursynth" / "plugins" / "vsanalog"
        / f"vsanalog{suffix}"
    )


def _ensure_plugin_loaded() -> None:
    """Load the vsanalog VapourSynth plugin if it isn't already available."""
    if not hasattr(vs.core, "analog"):
        plugin_path = _get_plugin_path()
        if not plugin_path.is_file():
            raise FileNotFoundError(
                f"vsanalog plugin not found at {plugin_path}. "
                "Ensure the vsanalog package is properly installed."
            )
        vs.core.std.LoadPlugin(plugin_path)


def requires_plugin(func: Callable[P, R]) -> Callable[P, R]:
    """Decorator ensuring the vsanalog VapourSynth plugin is loaded."""

    @functools.wraps(func)
    def wrapper(*args: P.args, **kwargs: P.kwargs) -> R:
        _ensure_plugin_loaded()
        return func(*args, **kwargs)

    return wrapper
