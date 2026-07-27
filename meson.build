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
        # opened. Pass -Dforce_fallback_for= to link a system SQLite instead.
        'force_fallback_for=sqlite3',
    ]
)

# =====================
# Dependencies
# =====================

# VapourSynth
vapoursynth_dep = dependency('vapoursynth', version: '>=55')

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
# vapoursynth-analog plugin
# =====================

vsanalog_sources = files(
    'src/plugin.cpp',
    'src/analog4fsc.cpp',
    'src/chdlog.cpp',
    'src/chromadecsource.cpp',
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

# When building as a wheel, install to vapoursynth/plugins/ in site-packages
# so the plugin is auto-loaded by VapourSynth's site-packages plugin discovery.
# Otherwise, install to VapourSynth's system plugin directory via pkg-config.
if get_option('build_wheel')
    py = import('python').find_installation(pure: false)
    plugin_install_dir = py.get_install_dir() / 'vapoursynth' / 'plugins'
    subdir('python/vsanalog')
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
    ],
    install: true,
    install_dir: plugin_install_dir,
    name_prefix: '',  # Don't add 'lib' prefix on Unix
    cpp_args: ['-D_USE_MATH_DEFINES'],
    link_args: vsanalog_link_args,
    link_depends: vsanalog_symbol_file,
    gnu_symbol_visibility: 'hidden',
)
