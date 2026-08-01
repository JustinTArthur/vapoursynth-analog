py.install_sources(
    '__init__.py',
    'plugin.py',
    'dropouts.py',
    'secam.py',
    'py.typed',
    subdir: 'vsanalog',
)

py.install_sources(
    '__pyinstaller/__init__.py',
    '__pyinstaller/hook-vsanalog.py',
    subdir: 'vsanalog/__pyinstaller',
)

# Bundle the NN model weights when present. CI populates models/ from
# tools/models.lock (ONNX, or CoreML .mlpackage on macOS) before the wheel
# build; a plain source build with no models installs nothing extra.
fs = import('fs')
if fs.is_dir(meson.current_source_dir() / 'models')
    install_subdir(
        'models',
        install_dir: py.get_install_dir() / 'vsanalog',
        exclude_files: ['README.md'],
    )
endif
