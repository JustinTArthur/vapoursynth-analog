import platform
from pathlib import Path

_suffix = {"Windows": ".dll", "Darwin": ".dylib"}.get(platform.system(), ".so")
_packages_root = Path(__file__).resolve().parent.parent.parent
_plugin = _packages_root / "vapoursynth" / "plugins" / f"vsanalog{_suffix}"

if _plugin.is_file():
    binaries = [(str(_plugin), "vapoursynth/plugins")]
