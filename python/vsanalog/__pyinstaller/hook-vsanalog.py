import platform
from pathlib import Path

_suffix = {"Windows": ".dll", "Darwin": ".dylib"}.get(platform.system(), ".so")
_pkg_root = Path(__file__).resolve().parent.parent  # site-packages/vsanalog
_packages_root = _pkg_root.parent
_plugin_dir = _packages_root / "vapoursynth" / "plugins" / "vsanalog"
_plugin = _plugin_dir / f"vsanalog{_suffix}"

binaries = []
if _plugin.is_file():
    # The whole plugin directory: the plugin itself plus, on Windows, the
    # dependency DLLs co-located there in place of delvewheel's vsanalog.libs/.
    # The plugin resolves those from its own directory, so they have to travel
    # with it.
    binaries = [
        (str(p), "vapoursynth/plugins/vsanalog")
        for p in _plugin_dir.iterdir()
        if p.is_file() and p.name != "manifest.vs"
    ]

datas = []
# manifest.vs scopes VapourSynth's autoloader to the plugin, so the co-located
# dependencies are never probed as plugins in their own right.
_manifest = _plugin_dir / "manifest.vs"
if _manifest.is_file():
    datas.append((str(_manifest), "vapoursynth/plugins/vsanalog"))

# Bundle the NN model weights (ONNX, or CoreML .mlpackage bundles on macOS) so
# frozen apps can locate them next to the package at runtime.
_models = _pkg_root / "models"
if _models.is_dir():
    datas.append((str(_models), "vsanalog/models"))
