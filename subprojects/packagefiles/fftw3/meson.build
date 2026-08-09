# Meson port of FFTW 3.3.11 (double precision only), applied to the upstream
# tarball through subprojects/fftw3.wrap. Upstream ships autotools and a CMake
# build; neither is usable here — CMake has no NEON option at all, and
# autotools cannot drive MSVC. Codelet sources are pre-generated in the
# tarball, so this only has to compile them with the right per-ISA flags.
#
# Scope is deliberately narrow: the core double-precision library, no threads,
# no MPI, no Fortran, no single/long-double variants.
project('fftw3', 'c',
    version : '3.3.11',
    license : 'GPL-2.0-or-later',
    meson_version : '>=1.0',
    default_options : [
        'default_library=static',
        'warning_level=0',
        'c_std=none',
    ],
)

cc = meson.get_compiler('c')
cpu = host_machine.cpu_family()
msvc_syntax = cc.get_argument_syntax() == 'msvc'

# =====================
# config.h
# =====================

cdata = configuration_data()
cdata.set_quoted('PACKAGE', 'fftw')
cdata.set_quoted('PACKAGE_NAME', 'fftw')
cdata.set_quoted('PACKAGE_TARNAME', 'fftw')
cdata.set_quoted('PACKAGE_BUGREPORT', 'fftw@fftw.org')
cdata.set_quoted('PACKAGE_URL', '')
cdata.set_quoted('PACKAGE_VERSION', meson.project_version())
cdata.set_quoted('PACKAGE_STRING', 'fftw ' + meson.project_version())
cdata.set_quoted('VERSION', meson.project_version())
# Stamped into exported wisdom so a plan is not reused across toolchains.
cdata.set_quoted('FFTW_CC', cc.get_id() + ' ' + cc.version())
cdata.set('STDC_HEADERS', 1)
cdata.set('DISABLE_FORTRAN', 1)

check_headers = [
    'alloca.h', 'c_asm.h', 'dlfcn.h', 'fcntl.h', 'fenv.h', 'intrinsics.h',
    'inttypes.h', 'limits.h', 'malloc.h', 'stddef.h', 'stdint.h', 'stdio.h',
    'stdlib.h', 'string.h', 'strings.h', 'sys/stat.h', 'sys/time.h',
    'sys/types.h', 'unistd.h',
]
foreach h : check_headers
    if cc.has_header(h)
        cdata.set('HAVE_' + h.underscorify().to_upper(), 1)
    endif
endforeach
if cc.has_header('sys/time.h') and cc.has_header('time.h')
    cdata.set('TIME_WITH_SYS_TIME', 1)
endif

# Mirrors AC_CHECK_FUNCS in configure.ac. Prefixes matter: several of these are
# macros or need the declaring header to be linked against correctly.
check_funcs = [
    ['abort', '#include <stdlib.h>'],
    ['BSDgettimeofday', ''],
    ['clock_gettime', '#include <time.h>'],
    ['cosl', '#include <math.h>'],
    ['drand48', '#include <stdlib.h>'],
    ['gethrtime', ''],
    ['getpagesize', '#include <unistd.h>'],
    ['gettimeofday', '#include <sys/time.h>'],
    ['isnan', '#include <math.h>'],
    ['mach_absolute_time', ''],
    ['memalign', '#include <malloc.h>'],
    ['memmove', '#include <string.h>'],
    ['memset', '#include <string.h>'],
    ['posix_memalign', '#include <stdlib.h>'],
    ['read_real_time', ''],
    ['sinl', '#include <math.h>'],
    ['snprintf', '#include <stdio.h>'],
    ['sqrt', '#include <math.h>'],
    ['strchr', '#include <string.h>'],
    ['sysctl', '#include <sys/types.h>\n#include <sys/sysctl.h>'],
    ['time_base_to_time', ''],
    ['vprintf', '#include <stdio.h>'],
    ['_mm_free', '#include <xmmintrin.h>'],
    ['_mm_malloc', '#include <xmmintrin.h>'],
]
m_dep = cc.find_library('m', required : false)
foreach f : check_funcs
    if cc.has_function(f[0], prefix : f[1], dependencies : [m_dep])
        cdata.set('HAVE_' + f[0].to_upper(), 1)
    endif
endforeach
if m_dep.found()
    cdata.set('HAVE_LIBM', 1)
endif

# AC_CHECK_DECLS always defines these to 0 or 1; the sources test the value.
check_decls = [
    ['sinl', '#include <math.h>'],
    ['cosl', '#include <math.h>'],
    ['sinq', '#include <math.h>'],
    ['cosq', '#include <math.h>'],
    ['memalign', '#include <malloc.h>'],
    ['drand48', '#include <stdlib.h>'],
    ['srand48', '#include <stdlib.h>'],
    ['posix_memalign', '#include <stdlib.h>'],
]
foreach d : check_decls
    found = cc.compiles(d[1] + '\nvoid f(void) { (void) ' + d[0] + '; }',
                        name : 'declaration of ' + d[0])
    cdata.set('HAVE_DECL_' + d[0].to_upper(), found ? 1 : 0)
endforeach

foreach t : ['int', 'unsigned int', 'long', 'unsigned long', 'long long',
             'unsigned long long', 'size_t', 'ptrdiff_t', 'float', 'double']
    cdata.set('SIZEOF_' + t.underscorify().to_upper(), cc.sizeof(t))
endforeach
# Only consulted when uintptr_t is missing, but ifftw.h #errors on a zero.
cdata.set('SIZEOF_VOID_P', cc.sizeof('void *'))
cdata.set('SIZEOF_FFTW_R2R_KIND', cc.sizeof('fftw_r2r_kind',
    prefix : 'typedef enum { X } fftw_r2r_kind;'))

if cc.has_type('ptrdiff_t', prefix : '#include <stddef.h>')
    cdata.set('HAVE_PTRDIFF_T', 1)
endif
if cc.has_type('uintptr_t', prefix : '#include <stdint.h>')
    cdata.set('HAVE_UINTPTR_T', 1)
endif
if cc.has_type('long double')
    cdata.set('HAVE_LONG_DOUBLE', 1)
endif

# AC_FUNC_ALLOCA: __builtin_alloca on GCC/Clang, _alloca on MSVC, else the
# alloca.h declaration. FFTW only takes the fast stack path when both HAVE_ALLOCA
# and FFTW_ENABLE_ALLOCA are set.
alloca_probe = '''
#ifdef __GNUC__
# define alloca __builtin_alloca
#elif defined(_MSC_VER)
# include <malloc.h>
# define alloca _alloca
#elif defined(HAVE_ALLOCA_H)
# include <alloca.h>
#endif
#include <stddef.h>
int main(void) { void *p = alloca(16); return p != 0; }
'''
if cc.links(alloca_probe, name : 'alloca')
    cdata.set('HAVE_ALLOCA', 1)
    cdata.set('FFTW_ENABLE_ALLOCA', 1)
endif

# =====================
# SIMD codelet selection
# =====================
#
# FFTW picks x86 codelets at *run* time (simd-support/*-cpuid.h), so compiling
# SSE2/AVX/AVX2 in is safe on older CPUs. ARM has no such dispatch and the ISA is
# chosen at compile time, which is why only NEON — mandatory on aarch64 — is
# enabled here and SVE is not: an SVE build would fault on any part without it.

simd_isas = []
if get_option('simd')
    if cpu == 'x86' or cpu == 'x86_64'
        if msvc_syntax
            sse2_args = cpu == 'x86' ? ['/arch:SSE2'] : []
            avx_args = ['/arch:AVX']
            avx2_args = ['/arch:AVX2']
        else
            sse2_args = ['-msse2']
            avx_args = ['-mavx']
            avx2_args = ['-mavx2', '-mfma']
        endif
        simd_isas = [
            ['sse2', 'HAVE_SSE2', sse2_args, ['sse2'],
             '#include <emmintrin.h>\nint main(void){__m128d a=_mm_set1_pd(1.0);return (int)_mm_cvtsd_f64(a);}'],
            ['avx', 'HAVE_AVX', avx_args, ['avx'],
             '#include <immintrin.h>\nint main(void){__m256d a=_mm256_set1_pd(1.0);return (int)_mm256_cvtsd_f64(a);}'],
            ['avx2', 'HAVE_AVX2', avx2_args, ['avx2', 'avx2-128'],
             '#include <immintrin.h>\nint main(void){__m256d a=_mm256_set1_pd(1.0);a=_mm256_fmadd_pd(a,a,a);return (int)_mm256_cvtsd_f64(a);}'],
        ]
    elif cpu == 'aarch64'
        # NEON is baseline on aarch64 and needs no flag; -mfpu=neon is a 32-bit
        # ARM spelling. Double precision is supported here (see simd-neon.h).
        simd_isas = [
            ['neon', 'HAVE_NEON', [], ['neon'],
             '#include <arm_neon.h>\nint main(void){float64x2_t a=vdupq_n_f64(1.0);return (int)vgetq_lane_f64(a,0);}'],
        ]
    endif
endif

enabled_isas = []
foreach isa : simd_isas
    if cc.compiles(isa[4], args : isa[2], name : isa[0] + ' codelets')
        cdata.set(isa[1], 1)
        enabled_isas += [isa]
    endif
endforeach

config_h = configure_file(output : 'config.h', configuration : cdata)

fftw_inc = include_directories('.')
fftw_api_inc = include_directories('api')

# Upstream's own preference; -O3 comes from the Meson buildtype. -mtune=native is
# deliberately not copied over: these builds ship to other machines.
extra_c_args = cc.get_supported_arguments(
    '-fomit-frame-pointer', '-fstrict-aliasing')

# =====================
# Sources
# =====================
# Generated from the tarball's Makefile.am lists. Every dft/simd/<isa> and
# rdft/simd/<isa> directory ships the same codelet set, so one list serves all.

fftw_core_sources = files(
    'kernel/align.c',
    'kernel/alloc.c',
    'kernel/assert.c',
    'kernel/awake.c',
    'kernel/buffered.c',
    'kernel/cpy1d.c',
    'kernel/cpy2d-pair.c',
    'kernel/cpy2d.c',
    'kernel/ct.c',
    'kernel/debug.c',
    'kernel/extract-reim.c',
    'kernel/hash.c',
    'kernel/iabs.c',
    'kernel/kalloc.c',
    'kernel/md5-1.c',
    'kernel/md5.c',
    'kernel/minmax.c',
    'kernel/ops.c',
    'kernel/pickdim.c',
    'kernel/plan.c',
    'kernel/planner.c',
    'kernel/primes.c',
    'kernel/print.c',
    'kernel/problem.c',
    'kernel/rader.c',
    'kernel/scan.c',
    'kernel/solver.c',
    'kernel/solvtab.c',
    'kernel/stride.c',
    'kernel/tensor.c',
    'kernel/tensor1.c',
    'kernel/tensor2.c',
    'kernel/tensor3.c',
    'kernel/tensor4.c',
    'kernel/tensor5.c',
    'kernel/tensor7.c',
    'kernel/tensor8.c',
    'kernel/tensor9.c',
    'kernel/tile2d.c',
    'kernel/timer.c',
    'kernel/transpose.c',
    'kernel/trig.c',
    'kernel/twiddle.c',
    'dft/bluestein.c',
    'dft/buffered.c',
    'dft/conf.c',
    'dft/ct.c',
    'dft/dftw-direct.c',
    'dft/dftw-directsq.c',
    'dft/dftw-generic.c',
    'dft/dftw-genericbuf.c',
    'dft/direct.c',
    'dft/generic.c',
    'dft/indirect.c',
    'dft/indirect-transpose.c',
    'dft/kdft-dif.c',
    'dft/kdft-difsq.c',
    'dft/kdft-dit.c',
    'dft/kdft.c',
    'dft/nop.c',
    'dft/plan.c',
    'dft/problem.c',
    'dft/rader.c',
    'dft/rank-geq2.c',
    'dft/solve.c',
    'dft/vrank-geq1.c',
    'dft/zero.c',
    'dft/scalar/n.c',
    'dft/scalar/t.c',
    'dft/scalar/codelets/codlist.c',
    'dft/scalar/codelets/n1_10.c',
    'dft/scalar/codelets/n1_11.c',
    'dft/scalar/codelets/n1_12.c',
    'dft/scalar/codelets/n1_13.c',
    'dft/scalar/codelets/n1_14.c',
    'dft/scalar/codelets/n1_15.c',
    'dft/scalar/codelets/n1_16.c',
    'dft/scalar/codelets/n1_2.c',
    'dft/scalar/codelets/n1_20.c',
    'dft/scalar/codelets/n1_25.c',
    'dft/scalar/codelets/n1_3.c',
    'dft/scalar/codelets/n1_32.c',
    'dft/scalar/codelets/n1_4.c',
    'dft/scalar/codelets/n1_5.c',
    'dft/scalar/codelets/n1_6.c',
    'dft/scalar/codelets/n1_64.c',
    'dft/scalar/codelets/n1_7.c',
    'dft/scalar/codelets/n1_8.c',
    'dft/scalar/codelets/n1_9.c',
    'dft/scalar/codelets/q1_2.c',
    'dft/scalar/codelets/q1_3.c',
    'dft/scalar/codelets/q1_4.c',
    'dft/scalar/codelets/q1_5.c',
    'dft/scalar/codelets/q1_6.c',
    'dft/scalar/codelets/q1_8.c',
    'dft/scalar/codelets/t1_10.c',
    'dft/scalar/codelets/t1_12.c',
    'dft/scalar/codelets/t1_15.c',
    'dft/scalar/codelets/t1_16.c',
    'dft/scalar/codelets/t1_2.c',
    'dft/scalar/codelets/t1_20.c',
    'dft/scalar/codelets/t1_25.c',
    'dft/scalar/codelets/t1_3.c',
    'dft/scalar/codelets/t1_32.c',
    'dft/scalar/codelets/t1_4.c',
    'dft/scalar/codelets/t1_5.c',
    'dft/scalar/codelets/t1_6.c',
    'dft/scalar/codelets/t1_64.c',
    'dft/scalar/codelets/t1_7.c',
    'dft/scalar/codelets/t1_8.c',
    'dft/scalar/codelets/t1_9.c',
    'dft/scalar/codelets/t2_10.c',
    'dft/scalar/codelets/t2_16.c',
    'dft/scalar/codelets/t2_20.c',
    'dft/scalar/codelets/t2_25.c',
    'dft/scalar/codelets/t2_32.c',
    'dft/scalar/codelets/t2_4.c',
    'dft/scalar/codelets/t2_5.c',
    'dft/scalar/codelets/t2_64.c',
    'dft/scalar/codelets/t2_8.c',
    'rdft/hc2hc.c',
    'rdft/dft-r2hc.c',
    'rdft/dht-r2hc.c',
    'rdft/dht-rader.c',
    'rdft/buffered.c',
    'rdft/conf.c',
    'rdft/direct-r2r.c',
    'rdft/direct-r2c.c',
    'rdft/generic.c',
    'rdft/hc2hc-direct.c',
    'rdft/hc2hc-generic.c',
    'rdft/khc2hc.c',
    'rdft/kr2c.c',
    'rdft/kr2r.c',
    'rdft/indirect.c',
    'rdft/nop.c',
    'rdft/plan.c',
    'rdft/problem.c',
    'rdft/rank0.c',
    'rdft/rank-geq2.c',
    'rdft/rdft-dht.c',
    'rdft/solve.c',
    'rdft/vrank-geq1.c',
    'rdft/vrank3-transpose.c',
    'rdft/buffered2.c',
    'rdft/direct2.c',
    'rdft/nop2.c',
    'rdft/rank0-rdft2.c',
    'rdft/rank-geq2-rdft2.c',
    'rdft/plan2.c',
    'rdft/problem2.c',
    'rdft/solve2.c',
    'rdft/vrank-geq1-rdft2.c',
    'rdft/rdft2-rdft.c',
    'rdft/rdft2-tensor-max-index.c',
    'rdft/rdft2-inplace-strides.c',
    'rdft/rdft2-strides.c',
    'rdft/khc2c.c',
    'rdft/ct-hc2c.c',
    'rdft/ct-hc2c-direct.c',
    'rdft/scalar/hfb.c',
    'rdft/scalar/r2c.c',
    'rdft/scalar/r2r.c',
    'rdft/scalar/hc2c.c',
    'rdft/scalar/r2cf/codlist.c',
    'rdft/scalar/r2cf/hc2cf2_16.c',
    'rdft/scalar/r2cf/hc2cf2_20.c',
    'rdft/scalar/r2cf/hc2cf2_32.c',
    'rdft/scalar/r2cf/hc2cf2_4.c',
    'rdft/scalar/r2cf/hc2cf2_8.c',
    'rdft/scalar/r2cf/hc2cf_10.c',
    'rdft/scalar/r2cf/hc2cf_12.c',
    'rdft/scalar/r2cf/hc2cf_16.c',
    'rdft/scalar/r2cf/hc2cf_2.c',
    'rdft/scalar/r2cf/hc2cf_20.c',
    'rdft/scalar/r2cf/hc2cf_32.c',
    'rdft/scalar/r2cf/hc2cf_4.c',
    'rdft/scalar/r2cf/hc2cf_6.c',
    'rdft/scalar/r2cf/hc2cf_8.c',
    'rdft/scalar/r2cf/hc2cfdft2_16.c',
    'rdft/scalar/r2cf/hc2cfdft2_20.c',
    'rdft/scalar/r2cf/hc2cfdft2_32.c',
    'rdft/scalar/r2cf/hc2cfdft2_4.c',
    'rdft/scalar/r2cf/hc2cfdft2_8.c',
    'rdft/scalar/r2cf/hc2cfdft_10.c',
    'rdft/scalar/r2cf/hc2cfdft_12.c',
    'rdft/scalar/r2cf/hc2cfdft_16.c',
    'rdft/scalar/r2cf/hc2cfdft_2.c',
    'rdft/scalar/r2cf/hc2cfdft_20.c',
    'rdft/scalar/r2cf/hc2cfdft_32.c',
    'rdft/scalar/r2cf/hc2cfdft_4.c',
    'rdft/scalar/r2cf/hc2cfdft_6.c',
    'rdft/scalar/r2cf/hc2cfdft_8.c',
    'rdft/scalar/r2cf/hf2_16.c',
    'rdft/scalar/r2cf/hf2_20.c',
    'rdft/scalar/r2cf/hf2_25.c',
    'rdft/scalar/r2cf/hf2_32.c',
    'rdft/scalar/r2cf/hf2_4.c',
    'rdft/scalar/r2cf/hf2_5.c',
    'rdft/scalar/r2cf/hf2_8.c',
    'rdft/scalar/r2cf/hf_10.c',
    'rdft/scalar/r2cf/hf_12.c',
    'rdft/scalar/r2cf/hf_15.c',
    'rdft/scalar/r2cf/hf_16.c',
    'rdft/scalar/r2cf/hf_2.c',
    'rdft/scalar/r2cf/hf_20.c',
    'rdft/scalar/r2cf/hf_25.c',
    'rdft/scalar/r2cf/hf_3.c',
    'rdft/scalar/r2cf/hf_32.c',
    'rdft/scalar/r2cf/hf_4.c',
    'rdft/scalar/r2cf/hf_5.c',
    'rdft/scalar/r2cf/hf_6.c',
    'rdft/scalar/r2cf/hf_64.c',
    'rdft/scalar/r2cf/hf_7.c',
    'rdft/scalar/r2cf/hf_8.c',
    'rdft/scalar/r2cf/hf_9.c',
    'rdft/scalar/r2cf/r2cfII_10.c',
    'rdft/scalar/r2cf/r2cfII_12.c',
    'rdft/scalar/r2cf/r2cfII_15.c',
    'rdft/scalar/r2cf/r2cfII_16.c',
    'rdft/scalar/r2cf/r2cfII_2.c',
    'rdft/scalar/r2cf/r2cfII_20.c',
    'rdft/scalar/r2cf/r2cfII_25.c',
    'rdft/scalar/r2cf/r2cfII_3.c',
    'rdft/scalar/r2cf/r2cfII_32.c',
    'rdft/scalar/r2cf/r2cfII_4.c',
    'rdft/scalar/r2cf/r2cfII_5.c',
    'rdft/scalar/r2cf/r2cfII_6.c',
    'rdft/scalar/r2cf/r2cfII_64.c',
    'rdft/scalar/r2cf/r2cfII_7.c',
    'rdft/scalar/r2cf/r2cfII_8.c',
    'rdft/scalar/r2cf/r2cfII_9.c',
    'rdft/scalar/r2cf/r2cf_10.c',
    'rdft/scalar/r2cf/r2cf_11.c',
    'rdft/scalar/r2cf/r2cf_12.c',
    'rdft/scalar/r2cf/r2cf_128.c',
    'rdft/scalar/r2cf/r2cf_13.c',
    'rdft/scalar/r2cf/r2cf_14.c',
    'rdft/scalar/r2cf/r2cf_15.c',
    'rdft/scalar/r2cf/r2cf_16.c',
    'rdft/scalar/r2cf/r2cf_2.c',
    'rdft/scalar/r2cf/r2cf_20.c',
    'rdft/scalar/r2cf/r2cf_25.c',
    'rdft/scalar/r2cf/r2cf_3.c',
    'rdft/scalar/r2cf/r2cf_32.c',
    'rdft/scalar/r2cf/r2cf_4.c',
    'rdft/scalar/r2cf/r2cf_5.c',
    'rdft/scalar/r2cf/r2cf_6.c',
    'rdft/scalar/r2cf/r2cf_64.c',
    'rdft/scalar/r2cf/r2cf_7.c',
    'rdft/scalar/r2cf/r2cf_8.c',
    'rdft/scalar/r2cf/r2cf_9.c',
    'rdft/scalar/r2cb/codlist.c',
    'rdft/scalar/r2cb/hb2_16.c',
    'rdft/scalar/r2cb/hb2_20.c',
    'rdft/scalar/r2cb/hb2_25.c',
    'rdft/scalar/r2cb/hb2_32.c',
    'rdft/scalar/r2cb/hb2_4.c',
    'rdft/scalar/r2cb/hb2_5.c',
    'rdft/scalar/r2cb/hb2_8.c',
    'rdft/scalar/r2cb/hb_10.c',
    'rdft/scalar/r2cb/hb_12.c',
    'rdft/scalar/r2cb/hb_15.c',
    'rdft/scalar/r2cb/hb_16.c',
    'rdft/scalar/r2cb/hb_2.c',
    'rdft/scalar/r2cb/hb_20.c',
    'rdft/scalar/r2cb/hb_25.c',
    'rdft/scalar/r2cb/hb_3.c',
    'rdft/scalar/r2cb/hb_32.c',
    'rdft/scalar/r2cb/hb_4.c',
    'rdft/scalar/r2cb/hb_5.c',
    'rdft/scalar/r2cb/hb_6.c',
    'rdft/scalar/r2cb/hb_64.c',
    'rdft/scalar/r2cb/hb_7.c',
    'rdft/scalar/r2cb/hb_8.c',
    'rdft/scalar/r2cb/hb_9.c',
    'rdft/scalar/r2cb/hc2cb2_16.c',
    'rdft/scalar/r2cb/hc2cb2_20.c',
    'rdft/scalar/r2cb/hc2cb2_32.c',
    'rdft/scalar/r2cb/hc2cb2_4.c',
    'rdft/scalar/r2cb/hc2cb2_8.c',
    'rdft/scalar/r2cb/hc2cb_10.c',
    'rdft/scalar/r2cb/hc2cb_12.c',
    'rdft/scalar/r2cb/hc2cb_16.c',
    'rdft/scalar/r2cb/hc2cb_2.c',
    'rdft/scalar/r2cb/hc2cb_20.c',
    'rdft/scalar/r2cb/hc2cb_32.c',
    'rdft/scalar/r2cb/hc2cb_4.c',
    'rdft/scalar/r2cb/hc2cb_6.c',
    'rdft/scalar/r2cb/hc2cb_8.c',
    'rdft/scalar/r2cb/hc2cbdft2_16.c',
    'rdft/scalar/r2cb/hc2cbdft2_20.c',
    'rdft/scalar/r2cb/hc2cbdft2_32.c',
    'rdft/scalar/r2cb/hc2cbdft2_4.c',
    'rdft/scalar/r2cb/hc2cbdft2_8.c',
    'rdft/scalar/r2cb/hc2cbdft_10.c',
    'rdft/scalar/r2cb/hc2cbdft_12.c',
    'rdft/scalar/r2cb/hc2cbdft_16.c',
    'rdft/scalar/r2cb/hc2cbdft_2.c',
    'rdft/scalar/r2cb/hc2cbdft_20.c',
    'rdft/scalar/r2cb/hc2cbdft_32.c',
    'rdft/scalar/r2cb/hc2cbdft_4.c',
    'rdft/scalar/r2cb/hc2cbdft_6.c',
    'rdft/scalar/r2cb/hc2cbdft_8.c',
    'rdft/scalar/r2cb/r2cbIII_10.c',
    'rdft/scalar/r2cb/r2cbIII_12.c',
    'rdft/scalar/r2cb/r2cbIII_15.c',
    'rdft/scalar/r2cb/r2cbIII_16.c',
    'rdft/scalar/r2cb/r2cbIII_2.c',
    'rdft/scalar/r2cb/r2cbIII_20.c',
    'rdft/scalar/r2cb/r2cbIII_25.c',
    'rdft/scalar/r2cb/r2cbIII_3.c',
    'rdft/scalar/r2cb/r2cbIII_32.c',
    'rdft/scalar/r2cb/r2cbIII_4.c',
    'rdft/scalar/r2cb/r2cbIII_5.c',
    'rdft/scalar/r2cb/r2cbIII_6.c',
    'rdft/scalar/r2cb/r2cbIII_64.c',
    'rdft/scalar/r2cb/r2cbIII_7.c',
    'rdft/scalar/r2cb/r2cbIII_8.c',
    'rdft/scalar/r2cb/r2cbIII_9.c',
    'rdft/scalar/r2cb/r2cb_10.c',
    'rdft/scalar/r2cb/r2cb_11.c',
    'rdft/scalar/r2cb/r2cb_12.c',
    'rdft/scalar/r2cb/r2cb_128.c',
    'rdft/scalar/r2cb/r2cb_13.c',
    'rdft/scalar/r2cb/r2cb_14.c',
    'rdft/scalar/r2cb/r2cb_15.c',
    'rdft/scalar/r2cb/r2cb_16.c',
    'rdft/scalar/r2cb/r2cb_2.c',
    'rdft/scalar/r2cb/r2cb_20.c',
    'rdft/scalar/r2cb/r2cb_25.c',
    'rdft/scalar/r2cb/r2cb_3.c',
    'rdft/scalar/r2cb/r2cb_32.c',
    'rdft/scalar/r2cb/r2cb_4.c',
    'rdft/scalar/r2cb/r2cb_5.c',
    'rdft/scalar/r2cb/r2cb_6.c',
    'rdft/scalar/r2cb/r2cb_64.c',
    'rdft/scalar/r2cb/r2cb_7.c',
    'rdft/scalar/r2cb/r2cb_8.c',
    'rdft/scalar/r2cb/r2cb_9.c',
    'rdft/scalar/r2r/codlist.c',
    'rdft/scalar/r2r/e01_8.c',
    'rdft/scalar/r2r/e10_8.c',
    'reodft/conf.c',
    'reodft/reodft010e-r2hc.c',
    'reodft/reodft11e-radix2.c',
    'reodft/reodft11e-r2hc-odd.c',
    'reodft/redft00e-r2hc-pad.c',
    'reodft/rodft00e-r2hc-pad.c',
    'reodft/reodft00e-splitradix.c',
    'api/apiplan.c',
    'api/configure.c',
    'api/execute-dft-c2r.c',
    'api/execute-dft-r2c.c',
    'api/execute-dft.c',
    'api/execute-r2r.c',
    'api/execute-split-dft-c2r.c',
    'api/execute-split-dft-r2c.c',
    'api/execute-split-dft.c',
    'api/execute.c',
    'api/export-wisdom-to-file.c',
    'api/export-wisdom-to-string.c',
    'api/export-wisdom.c',
    'api/f77api.c',
    'api/flops.c',
    'api/forget-wisdom.c',
    'api/import-system-wisdom.c',
    'api/import-wisdom-from-file.c',
    'api/import-wisdom-from-string.c',
    'api/import-wisdom.c',
    'api/malloc.c',
    'api/map-r2r-kind.c',
    'api/mapflags.c',
    'api/mkprinter-file.c',
    'api/mkprinter-str.c',
    'api/mktensor-iodims.c',
    'api/mktensor-rowmajor.c',
    'api/plan-dft-1d.c',
    'api/plan-dft-2d.c',
    'api/plan-dft-3d.c',
    'api/plan-dft-c2r-1d.c',
    'api/plan-dft-c2r-2d.c',
    'api/plan-dft-c2r-3d.c',
    'api/plan-dft-c2r.c',
    'api/plan-dft-r2c-1d.c',
    'api/plan-dft-r2c-2d.c',
    'api/plan-dft-r2c-3d.c',
    'api/plan-dft-r2c.c',
    'api/plan-dft.c',
    'api/plan-guru-dft-c2r.c',
    'api/plan-guru-dft-r2c.c',
    'api/plan-guru-dft.c',
    'api/plan-guru-r2r.c',
    'api/plan-guru-split-dft-c2r.c',
    'api/plan-guru-split-dft-r2c.c',
    'api/plan-guru-split-dft.c',
    'api/plan-many-dft-c2r.c',
    'api/plan-many-dft-r2c.c',
    'api/plan-many-dft.c',
    'api/plan-many-r2r.c',
    'api/plan-r2r-1d.c',
    'api/plan-r2r-2d.c',
    'api/plan-r2r-3d.c',
    'api/plan-r2r.c',
    'api/print-plan.c',
    'api/rdft2-pad.c',
    'api/the-planner.c',
    'api/version.c',
    'api/plan-guru64-dft-c2r.c',
    'api/plan-guru64-dft-r2c.c',
    'api/plan-guru64-dft.c',
    'api/plan-guru64-r2r.c',
    'api/plan-guru64-split-dft-c2r.c',
    'api/plan-guru64-split-dft-r2c.c',
    'api/plan-guru64-split-dft.c',
    'api/mktensor-iodims64.c',
    'simd-support/taint.c',
    'simd-support/sse2.c',
    'simd-support/avx.c',
    'simd-support/avx-128-fma.c',
    'simd-support/avx2.c',
    'simd-support/avx512.c',
    'simd-support/kcvi.c',
    'simd-support/altivec.c',
    'simd-support/vsx.c',
    'simd-support/neon.c',
    'simd-support/lsx.c',
    'simd-support/lasx.c',
    'simd-support/sve.c',
)

dft_simd_codelets = [
    'codlist.c',
    'genus.c',
    'n1bv_10.c',
    'n1bv_11.c',
    'n1bv_12.c',
    'n1bv_128.c',
    'n1bv_13.c',
    'n1bv_14.c',
    'n1bv_15.c',
    'n1bv_16.c',
    'n1bv_2.c',
    'n1bv_20.c',
    'n1bv_25.c',
    'n1bv_3.c',
    'n1bv_32.c',
    'n1bv_4.c',
    'n1bv_5.c',
    'n1bv_6.c',
    'n1bv_64.c',
    'n1bv_7.c',
    'n1bv_8.c',
    'n1bv_9.c',
    'n1fv_10.c',
    'n1fv_11.c',
    'n1fv_12.c',
    'n1fv_128.c',
    'n1fv_13.c',
    'n1fv_14.c',
    'n1fv_15.c',
    'n1fv_16.c',
    'n1fv_2.c',
    'n1fv_20.c',
    'n1fv_25.c',
    'n1fv_3.c',
    'n1fv_32.c',
    'n1fv_4.c',
    'n1fv_5.c',
    'n1fv_6.c',
    'n1fv_64.c',
    'n1fv_7.c',
    'n1fv_8.c',
    'n1fv_9.c',
    'n2bv_10.c',
    'n2bv_12.c',
    'n2bv_14.c',
    'n2bv_16.c',
    'n2bv_2.c',
    'n2bv_20.c',
    'n2bv_32.c',
    'n2bv_4.c',
    'n2bv_6.c',
    'n2bv_64.c',
    'n2bv_8.c',
    'n2fv_10.c',
    'n2fv_12.c',
    'n2fv_14.c',
    'n2fv_16.c',
    'n2fv_2.c',
    'n2fv_20.c',
    'n2fv_32.c',
    'n2fv_4.c',
    'n2fv_6.c',
    'n2fv_64.c',
    'n2fv_8.c',
    'n2sv_16.c',
    'n2sv_32.c',
    'n2sv_4.c',
    'n2sv_64.c',
    'n2sv_8.c',
    'q1bv_2.c',
    'q1bv_4.c',
    'q1bv_5.c',
    'q1bv_8.c',
    'q1fv_2.c',
    'q1fv_4.c',
    'q1fv_5.c',
    'q1fv_8.c',
    't1buv_10.c',
    't1buv_2.c',
    't1buv_3.c',
    't1buv_4.c',
    't1buv_5.c',
    't1buv_6.c',
    't1buv_7.c',
    't1buv_8.c',
    't1buv_9.c',
    't1bv_10.c',
    't1bv_12.c',
    't1bv_15.c',
    't1bv_16.c',
    't1bv_2.c',
    't1bv_20.c',
    't1bv_25.c',
    't1bv_3.c',
    't1bv_32.c',
    't1bv_4.c',
    't1bv_5.c',
    't1bv_6.c',
    't1bv_64.c',
    't1bv_7.c',
    't1bv_8.c',
    't1bv_9.c',
    't1fuv_10.c',
    't1fuv_2.c',
    't1fuv_3.c',
    't1fuv_4.c',
    't1fuv_5.c',
    't1fuv_6.c',
    't1fuv_7.c',
    't1fuv_8.c',
    't1fuv_9.c',
    't1fv_10.c',
    't1fv_12.c',
    't1fv_15.c',
    't1fv_16.c',
    't1fv_2.c',
    't1fv_20.c',
    't1fv_25.c',
    't1fv_3.c',
    't1fv_32.c',
    't1fv_4.c',
    't1fv_5.c',
    't1fv_6.c',
    't1fv_64.c',
    't1fv_7.c',
    't1fv_8.c',
    't1fv_9.c',
    't1sv_16.c',
    't1sv_2.c',
    't1sv_32.c',
    't1sv_4.c',
    't1sv_8.c',
    't2bv_10.c',
    't2bv_16.c',
    't2bv_2.c',
    't2bv_20.c',
    't2bv_25.c',
    't2bv_32.c',
    't2bv_4.c',
    't2bv_5.c',
    't2bv_64.c',
    't2bv_8.c',
    't2fv_10.c',
    't2fv_16.c',
    't2fv_2.c',
    't2fv_20.c',
    't2fv_25.c',
    't2fv_32.c',
    't2fv_4.c',
    't2fv_5.c',
    't2fv_64.c',
    't2fv_8.c',
    't2sv_16.c',
    't2sv_32.c',
    't2sv_4.c',
    't2sv_8.c',
    't3bv_10.c',
    't3bv_16.c',
    't3bv_20.c',
    't3bv_25.c',
    't3bv_32.c',
    't3bv_4.c',
    't3bv_5.c',
    't3bv_8.c',
    't3fv_10.c',
    't3fv_16.c',
    't3fv_20.c',
    't3fv_25.c',
    't3fv_32.c',
    't3fv_4.c',
    't3fv_5.c',
    't3fv_8.c',
]

rdft_simd_codelets = [
    'codlist.c',
    'genus.c',
    'hc2cbdftv_10.c',
    'hc2cbdftv_12.c',
    'hc2cbdftv_16.c',
    'hc2cbdftv_2.c',
    'hc2cbdftv_20.c',
    'hc2cbdftv_32.c',
    'hc2cbdftv_4.c',
    'hc2cbdftv_6.c',
    'hc2cbdftv_8.c',
    'hc2cfdftv_10.c',
    'hc2cfdftv_12.c',
    'hc2cfdftv_16.c',
    'hc2cfdftv_2.c',
    'hc2cfdftv_20.c',
    'hc2cfdftv_32.c',
    'hc2cfdftv_4.c',
    'hc2cfdftv_6.c',
    'hc2cfdftv_8.c',
]

# =====================
# Targets
# =====================

# Codelets are grouped per ISA so the vector flags never reach the dispatch code
# in simd-support/, which has to stay runnable on a CPU without the extension.
# LTO is forced off for the same reason: it would let the linker inline an AVX2
# codelet into a caller compiled for the baseline and fault on older hardware.
simd_objects = []
foreach isa : enabled_isas
    isa_sources = []
    foreach d : isa[3]
        foreach f : dft_simd_codelets
            isa_sources += 'dft/simd/' + d + '/' + f
        endforeach
        foreach f : rdft_simd_codelets
            isa_sources += 'rdft/simd/' + d + '/' + f
        endforeach
    endforeach
    simd_objects += static_library(
        'fftw3_' + isa[0].underscorify() + '_codelets',
        isa_sources,
        config_h,
        include_directories : fftw_inc,
        c_args : isa[2] + extra_c_args,
        gnu_symbol_visibility : 'hidden',
        override_options : ['b_lto=false'],
        pic : true,
    ).extract_all_objects(recursive : false)
endforeach

libfftw3 = static_library('fftw3',
    fftw_core_sources,
    config_h,
    objects : simd_objects,
    include_directories : fftw_inc,
    c_args : extra_c_args,
    gnu_symbol_visibility : 'hidden',
    override_options : ['b_lto=false'],
    pic : true,
    install : false,
)

fftw3_dep = declare_dependency(
    link_with : libfftw3,
    include_directories : fftw_api_inc,
    dependencies : m_dep.found() ? [m_dep] : [],
)

meson.override_dependency('fftw3', fftw3_dep)
