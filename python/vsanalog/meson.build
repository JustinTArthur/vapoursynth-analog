py.install_sources(
    '__init__.py',
    'py.typed',
    subdir: 'vsanalog',
)

py.install_sources(
    '__pyinstaller/__init__.py',
    '__pyinstaller/hook-vsanalog.py',
    subdir: 'vsanalog/__pyinstaller',
)

py.install_sources(
    'models/nntransform3d_v1.onnx',
    'models/nntransform3d_v2.onnx',
    'models/ldzeug2_luma_sep_field.onnx',
    'models/ldzeug2_luma_sep_frame.onnx',
    'models/ldzeug2_color_cnn_v1.onnx',
    'models/ldzeug2_color_cnn_v1_denoise.onnx',
    'models/ldzeug2_color_cnn_v2.onnx',
    subdir: 'vsanalog/models',
)
