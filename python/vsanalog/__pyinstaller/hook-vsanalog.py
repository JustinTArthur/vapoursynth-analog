import platform
from pathlib import Path

_suffix = {"Windows": ".dll", "Darwin": ".dylib"}.get(platform.system(), ".so")
_packages_root = Path(__file__).resolve().parent.parent.parent
_plugin_dir = _packages_root / "vapoursynth" / "plugins" / "vsanalog"
_plugin = _plugin_dir / f"vsanalog{_suffix}"
_manifest = _plugin_dir / "manifest.vs"

if _plugin.is_file():
    binaries = [(str(_plugin), "vapoursynth/plugins/vsanalog")]
if _manifest.is_file():
    datas = [(str(_manifest), "vapoursynth/plugins/vsanalog")]
