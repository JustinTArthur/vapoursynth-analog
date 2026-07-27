import platform
from pathlib import Path

_suffix = {"Windows": ".dll", "Darwin": ".dylib"}.get(platform.system(), ".so")
_pkg_root = Path(__file__).resolve().parent.parent  # site-packages/vsanalog
_packages_root = _pkg_root.parent
_plugin = _packages_root / "vapoursynth" / "plugins" / f"vsanalog{_suffix}"

binaries = []
if _plugin.is_file():
    binaries = [(str(_plugin), "vapoursynth/plugins")]

# Bundle the NN model weights (ONNX, or CoreML .mlpackage bundles on macOS) so
# frozen apps can locate them next to the package at runtime.
datas = []
_models = _pkg_root / "models"
if _models.is_dir():
    datas = [(str(_models), "vsanalog/models")]
