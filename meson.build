project('vapoursynth-analog', 'cpp',
    version : '0.3.0a3',
    default_options : [
        'buildtype=release',
        'cpp_std=c++20',
        'warning_level=2'
    ]
)

# =====================
# Dependencies
# =====================

# VapourSynth
vapoursynth_dep = dependency('vapoursynth', version: '>=55')

# Qt6 Core (required by tbc-tools)
# Note: We use sqlite3 C API directly instead of Qt SQL to avoid symbol conflicts with PyQt
qt6 = import('qt6')
qt6_dep = dependency('qt6', modules: ['Core'], required: true, method: 'config-tool')

# FFTW3 (required by tbc-tools chroma decoders)
fftw3_dep = dependency('fftw3', required: true)

# SQLite3 (for metadata database access without Qt SQL)
sqlite3_dep = dependency('sqlite3', required: true)

# =====================
# ONNX Runtime (required; tbc-tools' chroma decoder uses ORT
# unconditionally outside its CUDA block, so we always link against it)
# =====================
# Linux uses the upstream Microsoft prebuilt (.so.1 SOVERSION already correct).
# macOS uses Homebrew, which historically lacks the .1 SOVERSION symlink and
# pins install_name to the full .X.Y.Z.dylib filename — CI patches that on the
# brew tree before invoking meson so the binary winds up referencing
# @rpath/libonnxruntime.1.dylib regardless of which 1.x patch version brew
# happens to ship. Windows resolves through vcpkg's pkg-config.
onnxruntime_dep = dependency('libonnxruntime', required: true)

# tbc-tools' comb.cpp uses dlopen/dlsym/dladdr to lazily probe for the CUDA
# execution provider at runtime. On glibc < 2.34 (e.g. manylinux_2_28) those
# live in libdl.so.2 rather than libc, so we must link it explicitly. No-op
# on macOS and Windows.
cpp_compiler = meson.get_compiler('cpp')
dl_dep = cpp_compiler.find_library('dl', required: false)

# =====================
# tbc-tools library paths (relative)
# =====================

tbc_tools_rel = 'extern' / 'tbc-tools'
ld_library_rel = tbc_tools_rel / 'src' / 'library'
ld_chroma_rel = tbc_tools_rel / 'src' / 'ld-chroma-decoder'

# Absolute paths for file() calls
tbc_tools_dir = meson.current_source_dir() / tbc_tools_rel
ld_library_dir = meson.current_source_dir() / ld_library_rel
ld_chroma_dir = meson.current_source_dir() / ld_chroma_rel

# =====================
# Patch tbc-tools (idempotent) so Comb::Configuration carries an
# nnModelPath we can set at runtime instead of relying on the embedded
# byte-blob the upstream CMake generates. See tools/patch_tbc_tools.py.
# =====================
py3_patch = import('python').find_installation('python3', required: true)
patch_result = run_command(
    py3_patch,
    meson.current_source_dir() / 'tools' / 'patch_tbc_tools.py',
    tbc_tools_dir,
    check: true,
)
message(patch_result.stdout().strip())

# `meson dist` (used by `python -m build --sdist`) doesn't recurse into git
# submodules. Without this hook the sdist would be missing the tbc-tools
# C++ sources entirely. The script copies a curated subset of the
# submodule into the dist tarball, deliberately skipping ci/cache/ so the
# upstream fork's multi-GB Windows vcpkg binary cache stays out of PyPI.
meson.add_dist_script(
    py3_patch,
    meson.current_source_dir() / 'tools' / 'sdist_include_submodule.py',
)

# =====================
# ld-decode TBC library (lddecode-library)
# =====================

# Note: sqliteio.cpp is excluded - we provide a stub header and use our own
# sqlite3-based metadata reader to avoid Qt SQL dependency (PyQt symbol conflicts)
lddecode_library_sources = files(
    ld_library_rel / 'tbc' / 'dropouts.cpp',
    ld_library_rel / 'tbc' / 'filters.cpp',
    ld_library_rel / 'tbc' / 'jsonio.cpp',
    ld_library_rel / 'tbc' / 'lddecodemetadata.cpp',
    ld_library_rel / 'tbc' / 'logging.cpp',
    ld_library_rel / 'tbc' / 'navigation.cpp',
    ld_library_rel / 'tbc' / 'sourceaudio.cpp',
    ld_library_rel / 'tbc' / 'sourcevideo.cpp',
    ld_library_rel / 'tbc' / 'vbidecoder.cpp',
    ld_library_rel / 'tbc' / 'videoiddecoder.cpp',
    ld_library_rel / 'tbc' / 'vitcdecoder.cpp',
)

# Stub directory for our sqliteio.h replacement (must come before tbc-tools includes)
stubs_inc = include_directories('src/stubs')

lddecode_library_inc = include_directories(
    ld_library_rel / 'tbc',
    ld_library_rel / 'filter',
    ld_library_rel,
)

# Generate MOC files for Qt classes
lddecode_library_moc = qt6.compile_moc(
    headers: files(
        ld_library_rel / 'tbc' / 'lddecodemetadata.h',
    ),
    include_directories: lddecode_library_inc,
    dependencies: qt6_dep,
    method: 'config-tool'
)

# Get git info for tbc-tools version macros
git_prog = find_program('git', required: false)
if git_prog.found()
    git_branch = run_command(git_prog, '-C', tbc_tools_dir, 'rev-parse', '--abbrev-ref', 'HEAD', check: false)
    git_commit = run_command(git_prog, '-C', tbc_tools_dir, 'log', '-1', '--format=%h', check: false)
    app_branch = git_branch.returncode() == 0 ? git_branch.stdout().strip() : 'unknown'
    app_commit = git_commit.returncode() == 0 ? git_commit.stdout().strip() : 'unknown'
else
    app_branch = 'unknown'
    app_commit = 'unknown'
endif

# Force-include our sqliteio stub before any source file includes the real one.
# This works because our stub defines SQLITEIO_H guard, so the real header becomes empty.
sqliteio_stub = meson.current_source_dir() / 'src' / 'stubs' / 'sqliteio.h'
cpp = meson.get_compiler('cpp')
if cpp.get_argument_syntax() == 'msvc'
    force_include_args = ['/FI' + sqliteio_stub]
else
    force_include_args = ['-include', sqliteio_stub]
endif

lddecode_cpp_args = [
    '-D_USE_MATH_DEFINES',
    '-DAPP_BRANCH="' + app_branch + '"',
    '-DAPP_COMMIT="' + app_commit + '"',
] + force_include_args

lddecode_library = static_library(
    'lddecode-library',
    lddecode_library_sources,
    lddecode_library_moc,
    include_directories: [stubs_inc, lddecode_library_inc],
    dependencies: [qt6_dep],
    cpp_args: lddecode_cpp_args,
    gnu_symbol_visibility: 'hidden',
)

lddecode_library_dep = declare_dependency(
    include_directories: lddecode_library_inc,
    link_with: lddecode_library,
    dependencies: [qt6_dep],
)

# =====================
# ld-decode chroma library (lddecode-chroma)
# =====================

lddecode_chroma_sources = files(
    ld_chroma_rel / 'decoder.cpp',
    ld_chroma_rel / 'decoderpool.cpp',
    ld_chroma_rel / 'comb.cpp',
    ld_chroma_rel / 'componentframe.cpp',
    ld_chroma_rel / 'framecanvas.cpp',
    ld_chroma_rel / 'outputwriter.cpp',
    ld_chroma_rel / 'palcolour.cpp',
    ld_chroma_rel / 'sourcefield.cpp',
    ld_chroma_rel / 'transformpal.cpp',
    ld_chroma_rel / 'transformpal2d.cpp',
    ld_chroma_rel / 'transformpal3d.cpp',
    ld_chroma_rel / 'monodecoder.cpp',
    ld_chroma_rel / 'ntscdecoder.cpp',
    ld_chroma_rel / 'paldecoder.cpp',
)

lddecode_chroma_inc = include_directories(
    ld_chroma_rel,
)

# Generate MOC files for Qt classes in chroma decoder
lddecode_chroma_moc = qt6.compile_moc(
    headers: files(
        ld_chroma_rel / 'decoder.h',
        ld_chroma_rel / 'decoderpool.h',
        ld_chroma_rel / 'monodecoder.h',
        ld_chroma_rel / 'ntscdecoder.h',
        ld_chroma_rel / 'paldecoder.h',
    ),
    include_directories: [lddecode_chroma_inc, lddecode_library_inc],
    dependencies: [qt6_dep, lddecode_library_dep],
    method: 'config-tool'
)

lddecode_chroma_deps = [qt6_dep, fftw3_dep, lddecode_library_dep, onnxruntime_dep, dl_dep]

lddecode_chroma = static_library(
    'lddecode-chroma',
    lddecode_chroma_sources,
    lddecode_chroma_moc,
    include_directories: [lddecode_chroma_inc, lddecode_library_inc],
    dependencies: lddecode_chroma_deps,
    cpp_args: lddecode_cpp_args,
    gnu_symbol_visibility: 'hidden',
)

lddecode_chroma_dep = declare_dependency(
    include_directories: lddecode_chroma_inc,
    link_with: lddecode_chroma,
    dependencies: lddecode_chroma_deps,
)

# =====================
# vapoursynth-analog plugin
# =====================

vsanalog_sources = files(
    'src/plugin.cpp',
    'src/analog4fsc.cpp',
    'src/tbcreader.cpp',
    'src/dropoutcorrector.cpp',
    'src/jsonconverter_wrapper.cpp',
    'src/sqlite3_metadata_reader.cpp',
    'src/ldzeug_decoders.cpp',
)

vsanalog_inc = include_directories('src')

# When building as a wheel, install into a dedicated subdirectory of
# vapoursynth/plugins/ in site-packages, alongside a manifest.vs. VapourSynth
# honours a manifest in a plugin subdirectory and loads only the plugin it
# names so the autoloader never probes the co-bundled dependency DLLs.
# Otherwise, install to VapourSynth's system plugin directory via pkg-config.
if get_option('build_wheel')
    py = import('python').find_installation(pure: false)
    plugin_install_dir = py.get_install_dir() / 'vapoursynth' / 'plugins' / 'vsanalog'
    install_data('manifest.vs', install_dir: plugin_install_dir)
    subdir('python/vsanalog')
else
    plugin_install_dir = vapoursynth_dep.get_variable(pkgconfig: 'libdir') / 'vapoursynth'
endif

vsanalog_cpp_args = ['-D_USE_MATH_DEFINES']

# Embed @loader_path as an install rpath on macOS so dyld (and delocate-wheel's
# repair pass) can resolve @rpath/libonnxruntime.1.dylib next to our binary.
vsanalog_install_rpath = ''
if host_machine.system() == 'darwin'
    vsanalog_install_rpath = '@loader_path'
endif

shared_library(
    'vsanalog',
    vsanalog_sources,
    include_directories: [vsanalog_inc, lddecode_chroma_inc, lddecode_library_inc],
    dependencies: [
        vapoursynth_dep.partial_dependency(compile_args: true, includes: true),
        qt6_dep,
        fftw3_dep,
        sqlite3_dep,
        lddecode_library_dep,
        lddecode_chroma_dep,
        onnxruntime_dep,
    ],
    install: true,
    install_dir: plugin_install_dir,
    install_rpath: vsanalog_install_rpath,
    build_rpath: vsanalog_install_rpath,
    name_prefix: '',  # Don't add 'lib' prefix on Unix
    cpp_args: vsanalog_cpp_args,
    gnu_symbol_visibility: 'hidden',
)
