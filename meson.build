project('vapoursynth-analog', 'cpp',
    # Single source of truth for the version, as libchromadec does it. Anything
    # that can't ask Meson — the CI artifact names, the docs, the tag check —
    # reads this file directly instead of parsing meson.build. src/version.h is
    # generated from it below; tools/check_version.sh gates the rest.
    version : files('VERSION.txt'),
    default_options : [
        'buildtype=release',
        'cpp_std=c++20',
        'warning_level=2',
        # Take libchromadec's bundled SQLite (a trimmed, core-SQL-only
        # amalgamation it folds into its own archive) over a system libsqlite3,
        # so the plugin needs no SQLite installed: a system copy would be a
        # load-time dependency whether or not a `.db` metadata sidecar is ever
        # opened.
        #
        # FFTW likewise comes from subprojects/fftw3.wrap rather than the
        # platform package, because the off-the-shelf builds ship the wrong
        # codelets: Homebrew and Fedora build aarch64 FFTW with no NEON at all,
        # and Fedora's x86_64 build has no AVX2/FMA. Transform-PAL and
        # nnTransform3D are FFTW-bound, so those codelets set their frame rate.
        # It also means an sdist install no longer silently loses Transform-PAL
        # on a machine with no FFTW installed.
        #
        # Pass -Dforce_fallback_for= to link the system copies instead.
        'force_fallback_for=sqlite3,fftw3',
    ]
)

# =====================
# Dependencies
# =====================

# VapourSynth
vapoursynth_dep = dependency('vapoursynth', version: '>=55')

# dlopen/dladdr for src/gpupreload.cpp and libchromadec's runtime provider
# loading. A no-op where they live in libc; still a separate libdl on
# glibc < 2.34, which the manylinux_2_28 x86_64 wheel builds against.
cpp = meson.get_compiler('cpp')
dl_dep = cpp.find_library('dl', required: false)

# libchromadec — the Qt-free C-ABI chroma decoder, pulled in as a Meson git
# wrap (see subprojects/chromadec.wrap) and linked statically. It vendors its
# own SQLite (absorbed via link_whole) and optionally links ONNX Runtime for
# the neural-network decoders. Everything the old ld-decode-tools + Qt6 +
# FFTW3 + system-SQLite graph used to provide now lives behind chd_* here.
is_darwin = host_machine.system() == 'darwin'
ep = get_option('ep')

# Map the selected execution provider to libchromadec's build options.
# with_cuda / with_rocm gate the native cuFFT / hipFFT pipelines; the DirectML
# and TensorRT ONNX Runtime EPs come from whichever ORT build is linked, not a
# Meson toggle. macOS always builds native CoreML with no ONNX Runtime.
# Build libchromadec as a static archive so it's absorbed into our plugin (with
# its vendored SQLite via link_whole), leaving no libchromadec.* to bundle. Its
# transitive deps (FFTW/ONNX Runtime/CoreML frameworks) propagate to our link.
chromadec_opts = [
    'with_tests=false',
    'with_examples=false',
    'default_library=static',
]
if is_darwin
    chromadec_opts += [
        'with_onnxruntime=false',
        'with_coreml=enabled',
        'with_cuda=disabled',
        'with_rocm=disabled',
    ]
elif ep == 'none'
    chromadec_opts += [
        'with_onnxruntime=false',
        'with_coreml=disabled',
        'with_cuda=disabled',
        'with_rocm=disabled',
    ]
else
    chromadec_opts += [
        'with_onnxruntime=true',
        'with_coreml=disabled',
        'with_cuda=' + (ep == 'cuda' ? 'enabled' : 'disabled'),
        'with_rocm=' + (ep == 'migraphx' ? 'enabled' : 'disabled'),
    ]
endif

chromadec_dep = dependency('chromadec', default_options: chromadec_opts)

# =====================
# GPU vendor-runtime preload table
# =====================

# The GPU wheels exclude the vendor runtime (CUDA, cuDNN, TensorRT) and expect
# the user to supply it; src/gpupreload.cpp and the wheel's
# _vsanalog_gpu_preload.py resolve pip-installed copies at load time. Both
# read the same table built here: each entry is the platform library name with
# its major baked in (the bundled ONNX Runtime pins the majors) and the
# site-packages-relative directories the pip packages use. Leaves first, so a
# preloaded library satisfies every later one's dependency on it. CUDA 12
# wheels install per-component directories (nvidia/<pkg>/lib) where CUDA 13
# consolidates under nvidia/cu13/, hence the per-major dir lists; TensorRT
# ships in tensorrt_libs on both. The cu13 wheels are multi-arch on Windows
# and put the DLLs one level down in bin/x86_64, unlike their Linux lib/.
gpu_runtime_name = 'none'
gpu_preload_entries = []
if ep == 'cuda' and not is_darwin
    cuda_dep = dependency('cuda', version: '>=12')
    cuda_major = cuda_dep.version().split('.')[0]
    gpu_runtime_name = 'cuda' + cuda_major
    if host_machine.system() == 'windows'
        if cuda_major == '13'
            gpu_preload_entries = [
                ['cudart64_13.dll',     ['nvidia/cu13/bin/x86_64', 'nvidia/cuda_runtime/bin']],
                ['nvrtc64_130_0.dll',   ['nvidia/cu13/bin/x86_64', 'nvidia/cuda_nvrtc/bin']],
                ['cublasLt64_13.dll',   ['nvidia/cu13/bin/x86_64', 'nvidia/cublas/bin']],
                ['cublas64_13.dll',     ['nvidia/cu13/bin/x86_64', 'nvidia/cublas/bin']],
                ['curand64_10.dll',     ['nvidia/cu13/bin/x86_64', 'nvidia/curand/bin']],
                ['cufft64_12.dll',      ['nvidia/cu13/bin/x86_64', 'nvidia/cufft/bin']],
                ['cudnn64_9.dll',       ['nvidia/cudnn/bin', 'nvidia/cu13/bin/x86_64']],
                ['nvinfer_10.dll',      ['tensorrt_libs']],
                ['nvonnxparser_10.dll', ['tensorrt_libs']],
            ]
        else
            gpu_preload_entries = [
                ['cudart64_12.dll',     ['nvidia/cuda_runtime/bin']],
                ['nvrtc64_120_0.dll',   ['nvidia/cuda_nvrtc/bin']],
                ['cublasLt64_12.dll',   ['nvidia/cublas/bin']],
                ['cublas64_12.dll',     ['nvidia/cublas/bin']],
                ['curand64_10.dll',     ['nvidia/curand/bin']],
                ['cufft64_11.dll',      ['nvidia/cufft/bin']],
                ['cudnn64_9.dll',       ['nvidia/cudnn/bin']],
                ['nvinfer_10.dll',      ['tensorrt_libs']],
                ['nvonnxparser_10.dll', ['tensorrt_libs']],
            ]
        endif
    else
        if cuda_major == '13'
            gpu_preload_entries = [
                ['libcudart.so.13',      ['nvidia/cu13/lib', 'nvidia/cuda_runtime/lib']],
                ['libnvrtc.so.13',       ['nvidia/cu13/lib', 'nvidia/cuda_nvrtc/lib']],
                ['libcublasLt.so.13',    ['nvidia/cu13/lib', 'nvidia/cublas/lib']],
                ['libcublas.so.13',      ['nvidia/cu13/lib', 'nvidia/cublas/lib']],
                ['libcurand.so.10',      ['nvidia/cu13/lib', 'nvidia/curand/lib']],
                ['libcufft.so.12',       ['nvidia/cu13/lib', 'nvidia/cufft/lib']],
                ['libcudnn.so.9',        ['nvidia/cudnn/lib', 'nvidia/cu13/lib']],
                ['libnvinfer.so.10',     ['tensorrt_libs']],
                ['libnvonnxparser.so.10', ['tensorrt_libs']],
            ]
        else
            gpu_preload_entries = [
                ['libcudart.so.12',      ['nvidia/cuda_runtime/lib']],
                ['libnvrtc.so.12',       ['nvidia/cuda_nvrtc/lib']],
                ['libcublasLt.so.12',    ['nvidia/cublas/lib']],
                ['libcublas.so.12',      ['nvidia/cublas/lib']],
                ['libcurand.so.10',      ['nvidia/curand/lib']],
                ['libcufft.so.11',       ['nvidia/cufft/lib']],
                ['libcudnn.so.9',        ['nvidia/cudnn/lib']],
                ['libnvinfer.so.10',     ['tensorrt_libs']],
                ['libnvonnxparser.so.10', ['tensorrt_libs']],
            ]
        endif
    endif
endif

# Runtime-dlopened families, resolved separately from the table above.
#
# cuDNN 9 is a thin loader shim over a family of sublibraries that it dlopens
# by bare soname when a kernel first needs one. Resolving libcudnn alone
# leaves the shim unable to find its engines, and ONNX Runtime surfaces that
# as a failure inside the Conv kernel rather than as a missing library — for
# nnTransform3D, a frame that silently decodes 2D instead. So the family has
# to be resolved too, leaves first (libcudnn_graph backs the rest).
#
# These load only when an accelerated backend is actually used, not at
# interpreter startup like the table above: the family is ~920 MB and costs
# ~170 MB of RSS to map, which no unrelated Python process in the environment
# should pay. Nothing here is a link-time dependency of the plugin, so
# deferring them changes nothing about plugin load; src/gpupreload.cpp pulls
# them in from the NN session path, ahead of any inference.
gpu_dlopen_entries = []
if ep == 'cuda' and not is_darwin
    cudnn_family = [
        'graph', 'ops', 'heuristic', 'engines_precompiled',
        'engines_runtime_compiled', 'engines_tensor_ir', 'adv', 'cnn', 'ext',
    ]
    if host_machine.system() == 'windows'
        cudnn_dirs = cuda_major == '13' ? ['nvidia/cudnn/bin', 'nvidia/cu13/bin/x86_64'] \
                                        : ['nvidia/cudnn/bin']
        foreach part : cudnn_family
            gpu_dlopen_entries += [['cudnn_' + part + '64_9.dll', cudnn_dirs]]
        endforeach
    else
        cudnn_dirs = cuda_major == '13' ? ['nvidia/cudnn/lib', 'nvidia/cu13/lib'] \
                                        : ['nvidia/cudnn/lib']
        foreach part : cudnn_family
            gpu_dlopen_entries += [['libcudnn_' + part + '.so.9', cudnn_dirs]]
        endforeach
    endif
endif

gpu_table_cxx = ''
gpu_table_py = '('
foreach entry : gpu_preload_entries
    dirs_cxx = ''
    dirs_py = ''
    foreach dir : entry[1]
        dirs_cxx += '"' + dir + '", '
        dirs_py += '"' + dir + '", '
    endforeach
    gpu_table_cxx += '{ "' + entry[0] + '", { ' + dirs_cxx + '} }, '
    gpu_table_py += '("' + entry[0] + '", (' + dirs_py + ')), '
endforeach
gpu_table_py += ')'

# The dlopen families reach the plugin the same way, and the Python module as
# data only — it never loads them, but tools/check_gpu_preload_table.py reads
# the table from there to check its coverage against the installed pip layout.
gpu_dlopen_cxx = ''
gpu_dlopen_py = '('
foreach entry : gpu_dlopen_entries
    dirs_cxx = ''
    dirs_py = ''
    foreach dir : entry[1]
        dirs_cxx += '"' + dir + '", '
        dirs_py += '"' + dir + '", '
    endforeach
    gpu_dlopen_cxx += '{ "' + entry[0] + '", { ' + dirs_cxx + '} }, '
    gpu_dlopen_py += '("' + entry[0] + '", (' + dirs_py + ')), '
endforeach
gpu_dlopen_py += ')'

# =====================
# vapoursynth-analog plugin
# =====================

vsanalog_sources = files(
    'src/plugin.cpp',
    'src/amplifychroma.cpp',
    'src/analog4fsc.cpp',
    'src/chdlog.cpp',
    'src/chromadecsource.cpp',
    'src/gpupreload.cpp',
    'src/modernizechromaticity.cpp',
    'src/resizeuv.cpp',
)

# The numeric components the plugin registration needs, taken off the front of
# each dotted field so a PEP 440 prerelease still yields integers: 0.3.0a4 is
# major 0, minor 3, patch 0. Meson has no regex, hence splitting on each marker.
version_fields = meson.project_version().split('.')
version_numbers = []
foreach index : [0, 1, 2]
    field = version_fields.length() > index ? version_fields[index] : '0'
    foreach marker : ['a', 'b', 'rc', 'post', 'dev', '+', '-']
        field = field.split(marker)[0]
    endforeach
    version_numbers += field == '' ? 0 : field.to_int()
endforeach

version_cfg = configuration_data()
version_cfg.set('VERSION_MAJOR', version_numbers[0])
version_cfg.set('VERSION_MINOR', version_numbers[1])
version_cfg.set('VERSION_PATCH', version_numbers[2])
version_cfg.set('VERSION_STRING', meson.project_version())
vsanalog_sources += configure_file(
    input: 'src/version.h.in',
    output: 'version.h',
    configuration: version_cfg,
)

gpupreload_cfg = configuration_data()
gpupreload_cfg.set('VSA_GPU_RUNTIME', gpu_runtime_name)
gpupreload_cfg.set('VSA_GPU_PRELOAD_TABLE', gpu_table_cxx)
gpupreload_cfg.set('VSA_GPU_DLOPEN_TABLE', gpu_dlopen_cxx)
vsanalog_sources += configure_file(
    input: 'src/gpupreload_config.h.in',
    output: 'gpupreload_config.h',
    configuration: gpupreload_cfg,
)

vsanalog_inc = include_directories('src')

# gnu_symbol_visibility only reaches our own sources; libchromadec is linked in
# statically with its chd_* symbols at default visibility, so without a linker
# script the plugin re-exports the whole C ABI. That matters beyond tidiness:
# libchromadec's log sink, chd_init state and ONNX Runtime environment are all
# process-wide, and on ELF a second plugin linking the same library could bind
# to our copy and share them. Meson's only export-list keyword is
# vs_module_defs, which the non-Windows linkers discard, so hand the flag over
# directly.
if is_darwin
    vsanalog_symbol_file = files('src/vsanalog.symbols')
    vsanalog_link_args = ['-Wl,-exported_symbols_list,'
                          + meson.current_source_dir() / 'src/vsanalog.symbols']
elif host_machine.system() == 'windows'
    # VS_EXTERNAL_API's dllexport is already the whole export list.
    vsanalog_symbol_file = []
    vsanalog_link_args = []
else
    vsanalog_symbol_file = files('src/vsanalog.map')
    vsanalog_link_args = ['-Wl,--version-script,'
                          + meson.current_source_dir() / 'src/vsanalog.map']
endif

# When building as a wheel, install into a dedicated subdirectory of
# vapoursynth/plugins/ in site-packages, alongside a manifest.vs. VapourSynth
# honours a manifest in a plugin subdirectory and loads only the plugin it
# names, so the autoloader never probes the co-bundled dependency DLLs. This
# needs VapourSynth R74 or newer; below that the wrapper's explicit LoadPlugin
# still works, but the plugin no longer autoloads. Outside a wheel, install to
# VapourSynth's system plugin directory via pkg-config.
if get_option('build_wheel')
    py = import('python').find_installation(pure: false)
    plugin_install_dir = py.get_install_dir() / 'vapoursynth' / 'plugins' / 'vsanalog'
    install_data('manifest.vs', install_dir: plugin_install_dir)
    subdir('python/vsanalog')

    # GPU wheels also install a preload module at the site-packages root plus
    # a .pth that runs it at interpreter startup — before VapourSynth's
    # autoloader can touch the plugin, whose own link-time CUDA dependencies
    # (with_cuda builds) must already resolve by then. Sited at the root, not
    # in the package, so the .pth never imports vsanalog (and with it
    # vapoursynth) into every interpreter in the environment.
    if gpu_preload_entries.length() > 0
        gpu_preload_py_cfg = configuration_data()
        gpu_preload_py_cfg.set('VSA_GPU_RUNTIME', gpu_runtime_name)
        gpu_preload_py_cfg.set('VSA_GPU_PRELOAD_TABLE_PY', gpu_table_py)
        gpu_preload_py_cfg.set('VSA_GPU_DLOPEN_TABLE_PY', gpu_dlopen_py)
        configure_file(
            input: 'python/_vsanalog_gpu_preload.py.in',
            output: '_vsanalog_gpu_preload.py',
            configuration: gpu_preload_py_cfg,
            install: true,
            install_dir: py.get_install_dir(),
        )
        install_data('python/vsanalog_gpu.pth', install_dir: py.get_install_dir())
    endif
else
    # Newer, Python-oriented VapourSynth drops libdir from its pkg-config file
    # (plugins autoload from site-packages); fall back to the standard libdir.
    plugin_install_dir = vapoursynth_dep.get_variable(
        pkgconfig: 'libdir',
        default_value: get_option('libdir'),
    ) / 'vapoursynth'
endif

shared_library(
    'vsanalog',
    vsanalog_sources,
    include_directories: [vsanalog_inc],
    dependencies: [
        vapoursynth_dep.partial_dependency(compile_args: true, includes: true),
        chromadec_dep,
        dl_dep,
    ],
    install: true,
    install_dir: plugin_install_dir,
    name_prefix: '',  # Don't add 'lib' prefix on Unix
    cpp_args: ['-D_USE_MATH_DEFINES'],
    link_args: vsanalog_link_args,
    link_depends: vsanalog_symbol_file,
    gnu_symbol_visibility: 'hidden',
    # libchromadec's archive carries CUDA objects on the GPU builds, which
    # would make Meson link the plugin with nvcc; its import-library naming
    # on Windows disagrees with what nvcc's link.exe produces, and no device
    # link step is needed (no relocatable device code), so keep the C++
    # linker throughout.
    link_language: 'cpp',
)
