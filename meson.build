project('vapoursynth-analog', 'cpp',
    version : '1.0.0',
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

# Qt6 Core (required by ld-decode)
# Note: We use sqlite3 C API directly instead of Qt SQL to avoid symbol conflicts with PyQt
qt6 = import('qt6')
qt6_dep = dependency('qt6', modules: ['Core'], required: true)

# FFTW3 (required by ld-decode chroma decoders)
fftw3_dep = dependency('fftw3', required: true)

# SQLite3 (for metadata database access without Qt SQL)
sqlite3_dep = dependency('sqlite3', required: true)

# =====================
# ld-decode library paths (relative)
# =====================

ld_decode_rel = 'extern' / 'ld-decode'
ld_library_rel = ld_decode_rel / 'tools' / 'library'
ld_chroma_rel = ld_decode_rel / 'tools' / 'ld-chroma-decoder'

# Absolute paths for file() calls
ld_decode_dir = meson.current_source_dir() / ld_decode_rel
ld_library_dir = meson.current_source_dir() / ld_library_rel
ld_chroma_dir = meson.current_source_dir() / ld_chroma_rel

# =====================
# ld-decode TBC library (lddecode-library)
# =====================

# Note: sqliteio.cpp is excluded - we provide a stub header and use our own
# sqlite3-based metadata reader to avoid Qt SQL dependency (PyQt symbol conflicts)
lddecode_library_sources = files(
    ld_library_rel / 'tbc' / 'dropouts.cpp',
    ld_library_rel / 'tbc' / 'filters.cpp',
    ld_library_rel / 'tbc' / 'lddecodemetadata.cpp',
    ld_library_rel / 'tbc' / 'logging.cpp',
    ld_library_rel / 'tbc' / 'navigation.cpp',
    ld_library_rel / 'tbc' / 'sourceaudio.cpp',
    ld_library_rel / 'tbc' / 'sourcevideo.cpp',
    ld_library_rel / 'tbc' / 'vbidecoder.cpp',
    ld_library_rel / 'tbc' / 'videoiddecoder.cpp',
    ld_library_rel / 'tbc' / 'vitcdecoder.cpp',
)

# Stub directory for our sqliteio.h replacement (must come before ld-decode includes)
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
)

# Get git info for ld-decode version macros
git_prog = find_program('git', required: false)
if git_prog.found()
    git_branch = run_command(git_prog, '-C', ld_decode_dir, 'rev-parse', '--abbrev-ref', 'HEAD', check: false)
    git_commit = run_command(git_prog, '-C', ld_decode_dir, 'log', '-1', '--format=%h', check: false)
    app_branch = git_branch.returncode() == 0 ? git_branch.stdout().strip() : 'unknown'
    app_commit = git_commit.returncode() == 0 ? git_commit.stdout().strip() : 'unknown'
else
    app_branch = 'unknown'
    app_commit = 'unknown'
endif

# Force-include our sqliteio stub before any source file includes the real one
# This works because our stub defines SQLITEIO_H guard, so the real header becomes empty
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
)

lddecode_library_dep = declare_dependency(
    include_directories: lddecode_library_inc,
    link_with: lddecode_library,
    dependencies: [qt6_dep],
)

# =====================
# JSON I/O from ld-decode (for JSON to SQLite conversion)
# =====================

ld_jsonconv_rel = ld_decode_rel / 'tools' / 'ld-json-converter'

# Only include directory, source is inlined in our wrapper
lddecode_jsonconv_inc = include_directories(
    ld_jsonconv_rel,
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
)

lddecode_chroma = static_library(
    'lddecode-chroma',
    lddecode_chroma_sources,
    lddecode_chroma_moc,
    include_directories: [lddecode_chroma_inc, lddecode_library_inc],
    dependencies: [qt6_dep, fftw3_dep, lddecode_library_dep],
    cpp_args: lddecode_cpp_args,
)

lddecode_chroma_dep = declare_dependency(
    include_directories: lddecode_chroma_inc,
    link_with: lddecode_chroma,
    dependencies: [qt6_dep, fftw3_dep, lddecode_library_dep],
)

# =====================
# vapoursynth-analog plugin
# =====================

vsanalog_sources = files(
    'src/plugin.cpp',
    'src/analog4fsc.cpp',
    'src/tbcreader.cpp',
    'src/jsonconverter_wrapper.cpp',
    'src/sqlite3_metadata_reader.cpp',
)

vsanalog_inc = include_directories('src')

# On macOS, we need to ensure our Qt libraries are loaded instead of PyQt's bundled Qt
# when running in environments like vspreview. Use RPATH to prioritize Homebrew's Qt.
if host_machine.system() == 'darwin'
    qt6_libdir = qt6_dep.get_variable(pkgconfig: 'libdir')
    vsanalog_link_args = ['-Wl,-rpath,' + qt6_libdir]
else
    vsanalog_link_args = []
endif

shared_library(
    'vsanalog',
    vsanalog_sources,
    # Note: lddecode_jsonconv_inc is intentionally NOT included here because it has
    # a different version of lddecodemetadata.h that lacks blanking16bIre member
    include_directories: [vsanalog_inc, lddecode_chroma_inc, lddecode_library_inc],
    dependencies: [
        vapoursynth_dep.partial_dependency(compile_args: true, includes: true),
        qt6_dep,
        fftw3_dep,
        sqlite3_dep,
        lddecode_library_dep,
        lddecode_chroma_dep,
    ],
    install: true,
    install_dir: vapoursynth_dep.get_variable(pkgconfig: 'libdir') / 'vapoursynth',
    name_prefix: '',  # Don't add 'lib' prefix on Unix
    cpp_args: ['-D_USE_MATH_DEFINES'],
    link_args: vsanalog_link_args,
)
