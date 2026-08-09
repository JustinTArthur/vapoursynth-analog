/******************************************************************************
 * modernizechromaticity.cpp
 * vapoursynth-analog - colorimetry/photometry modernization filter
 *
 * Converts analog-era colorimetry (NTSC-1953, PAL/EBU, SMPTE C, CRT phosphor
 * sets, Japanese studio practice...) to modern targets (BT.709, BT.2100 PQ,
 * BT.2020 SDR). Color only: no geometry, no resampling, no dithering.
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 ******************************************************************************/

#include "modernizechromaticity.h"

#include "resizeuv.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <cmath>
#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include <VapourSynth4.h>
#include <VSHelper4.h>

#if defined(__x86_64__) || defined(_M_X64)
    #define MODERNIZE_X86 1
    #include <immintrin.h>
    // clang-cl defines _MSC_VER but keeps Clang's codegen rules: AVX2
    // intrinsics need the target attribute, and _xgetbv is gated behind the
    // xsave feature. Only MSVC proper takes the intrin.h path.
    #if defined(_MSC_VER) && !defined(__clang__)
        #define MODERNIZE_MSVC 1
        #include <intrin.h>
        #define MODERNIZE_TARGET_AVX2
    #else
        #include <cpuid.h>
        #define MODERNIZE_TARGET_AVX2 __attribute__((target("avx2,fma")))
    #endif
#elif defined(__aarch64__) || defined(_M_ARM64)
    #define MODERNIZE_ARM64 1
    #include <arm_neon.h>
#endif

namespace {

// =====================
// Small double-precision linear algebra
// =====================

using Vec3 = std::array<double, 3>;
using Mat3 = std::array<Vec3, 3>;

Mat3 matMul(const Mat3 &a, const Mat3 &b) {
    Mat3 r{};
    for (int i = 0; i < 3; i++)
        for (int j = 0; j < 3; j++)
            for (int k = 0; k < 3; k++)
                r[i][j] += a[i][k] * b[k][j];
    return r;
}

Vec3 matMul(const Mat3 &a, const Vec3 &v) {
    Vec3 r{};
    for (int i = 0; i < 3; i++)
        for (int k = 0; k < 3; k++)
            r[i] += a[i][k] * v[k];
    return r;
}

Mat3 matInverse(const Mat3 &m) {
    const double det =
        m[0][0] * (m[1][1] * m[2][2] - m[1][2] * m[2][1]) -
        m[0][1] * (m[1][0] * m[2][2] - m[1][2] * m[2][0]) +
        m[0][2] * (m[1][0] * m[2][1] - m[1][1] * m[2][0]);
    const double id = 1.0 / det;
    Mat3 r;
    r[0][0] = (m[1][1] * m[2][2] - m[1][2] * m[2][1]) * id;
    r[0][1] = (m[0][2] * m[2][1] - m[0][1] * m[2][2]) * id;
    r[0][2] = (m[0][1] * m[1][2] - m[0][2] * m[1][1]) * id;
    r[1][0] = (m[1][2] * m[2][0] - m[1][0] * m[2][2]) * id;
    r[1][1] = (m[0][0] * m[2][2] - m[0][2] * m[2][0]) * id;
    r[1][2] = (m[0][2] * m[1][0] - m[0][0] * m[1][2]) * id;
    r[2][0] = (m[1][0] * m[2][1] - m[1][1] * m[2][0]) * id;
    r[2][1] = (m[0][1] * m[2][0] - m[0][0] * m[2][1]) * id;
    r[2][2] = (m[0][0] * m[1][1] - m[0][1] * m[1][0]) * id;
    return r;
}

constexpr Mat3 kIdentity = {{{1, 0, 0}, {0, 1, 0}, {0, 0, 1}}};

// =====================
// Chromaticities
// =====================

struct Xy { double x, y; };
struct Chromaticity { Xy r, g, b, w; };

bool operator==(const Xy &a, const Xy &b) { return a.x == b.x && a.y == b.y; }

// White points. The Japanese "D93" is ITU-R BT.2035's reference value.
constexpr Xy kWhite9300K27MPCD = {0.281, 0.311};
constexpr Xy kWhiteRCA8500K = {0.287, 0.316};
constexpr Xy kWhiteC = {0.310, 0.316};              // As NTSC-1953 / BT.470 state it
constexpr Xy kWhiteCPrecise = {0.31006, 0.31616};   // CIE Illuminant C
constexpr Xy kWhiteD93 = {0.2831, 0.2971};          // ITU-R BT.2035 D93
constexpr Xy kWhiteD65 = {0.3127, 0.3290};
constexpr Xy kWhiteD65Precise = {0.31272, 0.32903};
constexpr Xy kWhiteDCI = {0.314, 0.351};
constexpr Xy kWhiteE = {1.0 / 3.0, 1.0 / 3.0};

enum class InPrimaries {
    Ntsc1953, Bt470Japan, Bt1700Japan, Pal, SmpteC, CodePoint22,
    EciaXXA, EciaXXB, EciaXXC, EciaXXD, EciaXXE, EciaXXF, EciaXXG,
    RcaSulfide8500K, RcaSulfide9300K27MPCD, RcaSulfideC,
    RcaP22_4_67, RcaP22_5_61, RcaP22_9_65,
    SonyP22, StudioJapan, NederlandProposal,
};

Chromaticity inChromaticity(InPrimaries p) {
    switch (p) {
    case InPrimaries::Ntsc1953:
        return {{0.67, 0.33}, {0.21, 0.71}, {0.14, 0.08}, kWhiteC};
    case InPrimaries::Bt470Japan:  // NTSC-1953 primaries with a D93 white
        return {{0.67, 0.33}, {0.21, 0.71}, {0.14, 0.08}, kWhiteD93};
    case InPrimaries::Bt1700Japan:  // SMPTE C primaries with a D93 white
        return {{0.630, 0.340}, {0.310, 0.595}, {0.155, 0.070}, kWhiteD93};
    case InPrimaries::Pal:
        return {{0.64, 0.33}, {0.29, 0.60}, {0.15, 0.06}, kWhiteD65};
    case InPrimaries::SmpteC:
        return {{0.630, 0.340}, {0.310, 0.595}, {0.155, 0.070}, kWhiteD65};
    case InPrimaries::CodePoint22:  // H.273 code point 22; matches no known standard
        return {{0.630, 0.340}, {0.295, 0.605}, {0.155, 0.077}, kWhiteD65};
    // ECIA/TEPAC/JEDEC group XX ("P22") registered phosphor sets, in
    // registration order.
    case InPrimaries::EciaXXA:
        return {{0.674, 0.326}, {0.218, 0.712}, {0.146, 0.052}, kWhiteRCA8500K};
    case InPrimaries::EciaXXB:
        return {{0.663, 0.337}, {0.285, 0.600}, {0.155, 0.060}, kWhite9300K27MPCD};
    case InPrimaries::EciaXXC:
        return {{0.650, 0.325}, {0.260, 0.600}, {0.157, 0.047}, kWhite9300K27MPCD};
    case InPrimaries::EciaXXD:
        return {{0.628, 0.337}, {0.300, 0.600}, {0.150, 0.068}, kWhite9300K27MPCD};
    case InPrimaries::EciaXXE:
        return {{0.640, 0.335}, {0.330, 0.590}, {0.150, 0.070}, kWhite9300K27MPCD};
    case InPrimaries::EciaXXF:
        return {{0.623, 0.342}, {0.343, 0.591}, {0.155, 0.067}, kWhite9300K27MPCD};
    case InPrimaries::EciaXXG:
        return {{0.630, 0.345}, {0.297, 0.597}, {0.149, 0.070}, kWhite9300K27MPCD};
    // RCA all-sulfide commercial phosphor mixes. The white point was never
    // published; each entry assumes a different plausible one.
    case InPrimaries::RcaSulfide8500K:
        return {{0.663, 0.337}, {0.242, 0.529}, {0.146, 0.052}, kWhiteRCA8500K};
    case InPrimaries::RcaSulfide9300K27MPCD:
        return {{0.663, 0.337}, {0.242, 0.529}, {0.146, 0.052}, kWhite9300K27MPCD};
    case InPrimaries::RcaSulfideC:
        return {{0.663, 0.337}, {0.242, 0.529}, {0.146, 0.052}, kWhiteCPrecise};
    // RCA's own P22 taxonomy (not JEDEC 16 registrations).
    case InPrimaries::RcaP22_4_67:
        return {{0.660, 0.340}, {0.300, 0.600}, {0.152, 0.063}, kWhite9300K27MPCD};
    case InPrimaries::RcaP22_5_61:
        return {{0.639, 0.342}, {0.265, 0.585}, {0.155, 0.061}, kWhite9300K27MPCD};
    case InPrimaries::RcaP22_9_65:
        return {{0.676, 0.324}, {0.290, 0.590}, {0.155, 0.061}, kWhite9300K27MPCD};
    case InPrimaries::SonyP22:  // Sony CRT set with the Japanese white point
        return {{0.625, 0.340}, {0.280, 0.595}, {0.155, 0.070}, kWhiteD93};
    case InPrimaries::StudioJapan:  // ARIB TR-B9 pre-1996 studio practice
        return {{0.618, 0.350}, {0.280, 0.605}, {0.152, 0.063}, kWhiteD93};
    case InPrimaries::NederlandProposal:  // CCIR Doc. XI/194 compromise primaries
        return {{0.66, 0.33}, {0.25, 0.65}, {0.145, 0.07}, kWhiteD65Precise};
    }
    return {};  // Unreachable
}

enum class OutPrimaries { Bt709, Bt2020, P3Dci, P3D65, Xyz };

Chromaticity outChromaticity(OutPrimaries p) {
    switch (p) {
    case OutPrimaries::Bt709:
        return {{0.640, 0.330}, {0.300, 0.600}, {0.150, 0.060}, kWhiteD65};
    case OutPrimaries::Bt2020:
        return {{0.708, 0.292}, {0.170, 0.797}, {0.131, 0.046}, kWhiteD65};
    case OutPrimaries::P3Dci:
        return {{0.680, 0.320}, {0.265, 0.690}, {0.150, 0.060}, kWhiteDCI};
    case OutPrimaries::P3D65:
        return {{0.680, 0.320}, {0.265, 0.690}, {0.150, 0.060}, kWhiteD65};
    case OutPrimaries::Xyz:
        return {{1, 0}, {0, 1}, {0, 0}, kWhiteE};  // Placeholder; XYZ bypasses the gamut matrix
    }
    return {};
}

Vec3 xyToXYZ(const Xy &c) {
    return {c.x / c.y, 1.0, (1.0 - c.x - c.y) / c.y};
}

// RGB -> CIE XYZ from primaries + white (Lindbloom's derivation, as zimg).
Mat3 rgbToXyzMatrix(const Chromaticity &c) {
    Mat3 xyz;  // Columns R, G, B; rows X, Y, Z
    const Vec3 r = xyToXYZ(c.r), g = xyToXYZ(c.g), b = xyToXYZ(c.b);
    for (int i = 0; i < 3; i++) {
        xyz[i][0] = r[i];
        xyz[i][1] = g[i];
        xyz[i][2] = b[i];
    }
    const Vec3 s = matMul(matInverse(xyz), xyToXYZ(c.w));
    for (int i = 0; i < 3; i++) {
        xyz[i][0] *= s[0];
        xyz[i][1] *= s[1];
        xyz[i][2] *= s[2];
    }
    return xyz;
}

double dot(const Vec3 &a, const Vec3 &b) {
    return a[0] * b[0] + a[1] * b[1] + a[2] * b[2];
}

Vec3 cross(const Vec3 &a, const Vec3 &b) {
    return {a[1] * b[2] - a[2] * b[1],
            a[2] * b[0] - a[0] * b[2],
            a[0] * b[1] - a[1] * b[0]};
}

// Luma weights derived from the chromaticity, per H.273 Equations E-22 to
// E-27 — what the chromaticity-derived matrix code points signal.
void lumaWeightsFromPrimaries(const Chromaticity &c, double *kr, double *kb) {
    const Vec3 r = xyToXYZ(c.r), g = xyToXYZ(c.g), b = xyToXYZ(c.b);
    const Vec3 w = xyToXYZ(c.w);
    const Vec3 xRgb = {r[0], g[0], b[0]};
    const Vec3 yRgb = {r[1], g[1], b[1]};
    const Vec3 zRgb = {r[2], g[2], b[2]};
    const double det = dot(xRgb, cross(yRgb, zRgb));
    *kr = dot(w, cross(g, b)) / det;
    *kb = dot(w, cross(r, g)) / det;
}

// Bradford chromatic adaptation between white points, operating on XYZ.
Mat3 bradfordAdaptationMatrix(const Xy &from, const Xy &to) {
    if (from == to)
        return kIdentity;
    const Mat3 bradford = {{
        {  0.8951,  0.2664, -0.1614 },
        { -0.7502,  1.7135,  0.0367 },
        {  0.0389, -0.0685,  1.0296 },
    }};
    const Vec3 srcCone = matMul(bradford, xyToXYZ(from));
    const Vec3 dstCone = matMul(bradford, xyToXYZ(to));
    Mat3 gain{};
    for (int i = 0; i < 3; i++)
        gain[i][i] = dstCone[i] / srcCone[i];
    return matMul(matInverse(bradford), matMul(gain, bradford));
}

// =====================
// Y'CbCr matrices
// =====================

Mat3 rgbToYuvMatrix(double kr, double kb) {
    const double kg = 1.0 - kr - kb;
    const double uscale = 1.0 / (2.0 - 2.0 * kb);
    const double vscale = 1.0 / (2.0 - 2.0 * kr);
    return {{
        { kr, kg, kb },
        { -kr * uscale, -kg * uscale, (1.0 - kb) * uscale },
        { (1.0 - kr) * vscale, -kg * vscale, -kb * vscale },
    }};
}

// Input matrices: the classic NTSC-1953/FCC luma weights and the modern
// BT.470/ST 170/BT.601 ones. Output: BT.709 and BT.2020.
constexpr double kFccKr = 0.30, kFccKb = 0.11;
constexpr double kBt601Kr = 0.299, kBt601Kb = 0.114;
constexpr double kBt709Kr = 0.2126, kBt709Kb = 0.0722;
constexpr double kBt2020Kr = 0.2627, kBt2020Kb = 0.0593;

enum class InMatrix { AnalogClassic, AnalogModern };
enum class OutMatrix { Rgb, Bt709, Bt2020Ncl, Bt2020Cl, ChromaticityDerivedCl };

// =====================
// Transfer characteristics
// =====================

enum class InTransfer {
    Linear, Gamma22, Gamma28, St170Scene, St170Display,
    Bt1886Annex1, Bt1886Appendix1, Srgb,
};
// Bt2020Oetf is not selectable through transfer_s: it is the scene-referred
// curve a constant-luminance encode falls back to when no output transfer is
// named, i.e. the pairing BT.2020 itself defines.
enum class OutTransfer { Linear, Bt1886Annex1, Bt2020Oetf, Srgb, Pq, Hlg };

// BT.2020 Table 4's OETF, at the exact alpha/beta the table solves for rather
// than its rounded 10-/12-bit practical values (same curve shape as BT.709 and
// BT.601). Only the constant-luminance encode uses it; the non-constant path
// takes whatever transfer_s selects.
constexpr float kOetfAlpha = 1.09929682680944f;
constexpr float kOetfBeta = 0.018053968510807f;

float oetf2020(float x) {
    x = std::max(x, 0.0f);
    if (x < kOetfBeta)
        return 4.5f * x;
    return kOetfAlpha * std::pow(x, 0.45f) - (kOetfAlpha - 1.0f);
}

// SMPTE ST 2084 (PQ) constants.
constexpr float kPqM1 = 0.1593017578125f;
constexpr float kPqM2 = 78.84375f;
constexpr float kPqC1 = 0.8359375f;
constexpr float kPqC2 = 18.8515625f;
constexpr float kPqC3 = 18.6875f;

// ARIB STD-B67 / BT.2100 HLG constants.
constexpr float kHlgA = 0.17883277f;
constexpr float kHlgB = 0.28466892f;
constexpr float kHlgC = 0.55991073f;

// A resolved per-channel transfer step. Parameters are precomputed at
// pipeline-build time; Kind selects the loop in applyTransfer.
struct TransferOp {
    enum class Kind {
        Identity,
        PowToLinear,         // p0 = exponent (2.2 / 2.8 display gamma)
        St170ToLinear,       // ST 170 §5.2 reproducer EOTF == exact inverse OETF
        Bt1886ToLinear,      // p0 = a, p1 = b (Annex 1)
        Bt1886CrtToLinear,   // p0 = k, p1 = b, p2 = k*(Vc+b)^-0.4 (Appendix 1)
        SrgbToLinear,        // IEC 61966-2-1 EOTF
        Bt1886FromLinear,    // p0 = 1/a, p1 = b
        Bt2020OetfFromLinear,  // Scene-referred; the constant-luminance default
        SrgbFromLinear,      // IEC 61966-2-1 Equations 7 and 8
        PqFromLinear,        // p0 = nominal_luminance / 10000
        HlgFromLinear,       // p0 = nominal_luminance / 1000
    };
    Kind kind = Kind::Identity;
    float p0 = 0.0f, p1 = 0.0f, p2 = 0.0f;
};

constexpr float kBt1886Vc = 0.35f;  // Appendix 1 segment threshold

// Per-kind scalar evaluations. applyTransfer switches once and loops over one
// of these; the constant-luminance encode calls them per sample through
// transferSample, so both paths share exactly one definition of each curve.
inline float powToLinear(float v, const TransferOp &op) {
    return v < 0.0f ? 0.0f : std::pow(v, op.p0);
}
inline float st170ToLinear(float v, const TransferOp &) {
    // ST 170-2004 §5.2, with the standard's own published constants.
    v = std::max(v, 0.0f);
    return v < 0.0812f ? v / 4.5f : std::pow((v + 0.099f) / 1.099f, 1.0f / 0.45f);
}
inline float bt1886ToLinear(float v, const TransferOp &op) {
    return op.p0 * std::pow(std::max(v + op.p1, 0.0f), 2.4f);
}
inline float bt1886CrtToLinear(float v, const TransferOp &op) {
    const float t = std::max(v + op.p1, 0.0f);
    return v < kBt1886Vc ? op.p2 * std::pow(t, 3.0f) : op.p0 * std::pow(t, 2.6f);
}
inline float srgbToLinear(float v, const TransferOp &) {
    v = std::max(v, 0.0f);
    return v <= 0.04045f ? v / 12.92f : std::pow((v + 0.055f) / 1.055f, 2.4f);
}
inline float bt1886FromLinear(float v, const TransferOp &op) {
    return std::pow(std::max(v, 0.0f) * op.p0, 1.0f / 2.4f) - op.p1;
}
inline float bt2020OetfFromLinear(float v, const TransferOp &) {
    return oetf2020(v);
}
// IEC 61966-2-1 Equations 7 and 8, at the standard's own breakpoint. That
// 0.0031308 is the decode side's 0.04045 rounded back through the linear
// segment, so the two directions invert each other to within 5e-9.
constexpr float kSrgbLinearCut = 0.0031308f;
inline float srgbFromLinear(float v, const TransferOp &) {
    v = std::max(v, 0.0f);
    return v <= kSrgbLinearCut ? 12.92f * v
                               : 1.055f * std::pow(v, 1.0f / 2.4f) - 0.055f;
}
inline float pqFromLinear(float v, const TransferOp &op) {
    const float x = std::max(v * op.p0, 0.0f);
    const float xpow = std::pow(x, kPqM1);
    // Arrangement that avoids cancellation error (as zimg).
    const float num = (kPqC1 - 1.0f) + (kPqC2 - kPqC3) * xpow;
    const float den = 1.0f + kPqC3 * xpow;
    return std::pow(1.0f + num / den, kPqM2);
}
inline float hlgFromLinear(float v, const TransferOp &op) {
    // Display-referred: inverse of the BT.2100 1.2-power OOTF, then OETF.
    float x = std::max(v * op.p0, 0.0f);
    x = std::pow(x, 1.0f / 1.2f);
    return x <= (1.0f / 12.0f) ? std::sqrt(3.0f * x)
                               : kHlgA * std::log(12.0f * x - kHlgB) + kHlgC;
}

float transferSample(const TransferOp &op, float v) {
    switch (op.kind) {
    case TransferOp::Kind::Identity: return v;
    case TransferOp::Kind::PowToLinear: return powToLinear(v, op);
    case TransferOp::Kind::St170ToLinear: return st170ToLinear(v, op);
    case TransferOp::Kind::Bt1886ToLinear: return bt1886ToLinear(v, op);
    case TransferOp::Kind::Bt1886CrtToLinear: return bt1886CrtToLinear(v, op);
    case TransferOp::Kind::SrgbToLinear: return srgbToLinear(v, op);
    case TransferOp::Kind::Bt1886FromLinear: return bt1886FromLinear(v, op);
    case TransferOp::Kind::Bt2020OetfFromLinear: return bt2020OetfFromLinear(v, op);
    case TransferOp::Kind::SrgbFromLinear: return srgbFromLinear(v, op);
    case TransferOp::Kind::PqFromLinear: return pqFromLinear(v, op);
    case TransferOp::Kind::HlgFromLinear: return hlgFromLinear(v, op);
    }
    return v;  // Unreachable
}

#if defined(MODERNIZE_X86)

// Shared by the transfer and affine kernels below.
uint64_t xgetbv0() {
#if defined(MODERNIZE_MSVC)
    return _xgetbv(0);
#else
    uint32_t eax, edx;
    __asm__ volatile("xgetbv" : "=a"(eax), "=d"(edx) : "c"(0));
    return (static_cast<uint64_t>(edx) << 32) | eax;
#endif
}

bool cpuHasAvx2Fma() {
#if defined(MODERNIZE_MSVC)
    int info[4];
    __cpuid(info, 0);
    if (info[0] < 7)
        return false;
    __cpuid(info, 1);
    const bool fma = (info[2] >> 12) & 1;
    const bool osxsave = (info[2] >> 27) & 1;
    if (!fma || !osxsave)
        return false;
    if ((xgetbv0() & 0x6) != 0x6)  // XMM and YMM state enabled by the OS
        return false;
    __cpuidex(info, 7, 0);
    return (info[1] >> 5) & 1;  // AVX2
#else
    unsigned eax, ebx, ecx, edx;
    if (!__get_cpuid(1, &eax, &ebx, &ecx, &edx))
        return false;
    const bool fma = (ecx >> 12) & 1;
    const bool osxsave = (ecx >> 27) & 1;
    if (!fma || !osxsave)
        return false;
    if ((xgetbv0() & 0x6) != 0x6)  // XMM and YMM state enabled by the OS
        return false;
    if (!__get_cpuid_count(7, 0, &eax, &ebx, &ecx, &edx))
        return false;
    return (ebx >> 5) & 1;  // AVX2
#endif
}

#endif  // MODERNIZE_X86

#if defined(MODERNIZE_ARM64) || defined(MODERNIZE_X86)

// =====================
// Vectorized transfer kernels
// =====================
//
// pow(x, c) evaluated as exp2(c * log2(x)) with a minimax polynomial for each
// half. log2 reduces the mantissa to [sqrt(1/2), sqrt(2)) and evaluates
// f*R(f^2) with f = (m-1)/(m+1); exp2 splits off the integer part and
// evaluates 1 + f*S(f). Both forms are exact at their fixed points, so
// pow(1, c) returns exactly 1 and reference white cannot drift.
//
// Accuracy is bounded by float32's hold on c*log2(x) rather than by the
// polynomials. Against the scalar path the to-linear curves stay within
// 7.2e-07 (0.05 of a 16-bit code value); PQ reaches 3.8e-06 (0.25 of a 16-bit
// code value, 0.016 at 12-bit) because its m2 exponent amplifies whatever the
// inner pow leaves behind. The scalar functions above remain the definition of
// each curve and handle the row tails.

// f*R(f^2); max absolute error in log2 is 2.1e-12.
constexpr float kLog2P0 = 2.885390043e+00f, kLog2P1 = 9.617967010e-01f,
                kLog2P2 = 5.770835876e-01f, kLog2P3 = 4.116728604e-01f,
                kLog2P4 = 3.407287002e-01f;
// 1 + f*S(f) for |f| <= 0.5; max relative error 8.0e-11.
constexpr float kExp2P0 = 6.931471825e-01f, kExp2P1 = 2.402265072e-01f,
                kExp2P2 = 5.550410599e-02f, kExp2P3 = 9.618057869e-03f,
                kExp2P4 = 1.333388267e-03f, kExp2P5 = 1.546100684e-04f,
                kExp2P6 = 1.519557918e-05f;

#if defined(MODERNIZE_ARM64)

inline float32x4_t vecLog2(float32x4_t x) {
    const int32x4_t ix = vreinterpretq_s32_f32(x);
    int32x4_t e = vsubq_s32(vshrq_n_s32(ix, 23), vdupq_n_s32(127));
    float32x4_t m = vreinterpretq_f32_s32(
        vorrq_s32(vandq_s32(ix, vdupq_n_s32(0x007FFFFF)), vdupq_n_s32(0x3F800000)));
    const uint32x4_t big = vcgtq_f32(m, vdupq_n_f32(1.41421356f));
    m = vbslq_f32(big, vmulq_f32(m, vdupq_n_f32(0.5f)), m);
    e = vaddq_s32(e, vreinterpretq_s32_u32(vshrq_n_u32(big, 31)));
    const float32x4_t one = vdupq_n_f32(1.0f);
    const float32x4_t f = vdivq_f32(vsubq_f32(m, one), vaddq_f32(m, one));
    const float32x4_t s = vmulq_f32(f, f);
    float32x4_t p = vdupq_n_f32(kLog2P4);
    p = vfmaq_f32(vdupq_n_f32(kLog2P3), s, p);
    p = vfmaq_f32(vdupq_n_f32(kLog2P2), s, p);
    p = vfmaq_f32(vdupq_n_f32(kLog2P1), s, p);
    p = vfmaq_f32(vdupq_n_f32(kLog2P0), s, p);
    return vfmaq_f32(vcvtq_f32_s32(e), f, p);
}

// y is bounded before the exponent field is assembled: without that, any |k|
// over 127 writes into the sign bit and returns garbage instead of underflowing
// to zero, which a high gamma on a near-black input reaches easily.
inline float32x4_t vecExp2(float32x4_t y) {
    y = vminq_f32(vmaxq_f32(y, vdupq_n_f32(-127.0f)), vdupq_n_f32(128.0f));
    const float32x4_t k = vrndnq_f32(y);
    const float32x4_t f = vsubq_f32(y, k);
    float32x4_t p = vdupq_n_f32(kExp2P6);
    p = vfmaq_f32(vdupq_n_f32(kExp2P5), f, p);
    p = vfmaq_f32(vdupq_n_f32(kExp2P4), f, p);
    p = vfmaq_f32(vdupq_n_f32(kExp2P3), f, p);
    p = vfmaq_f32(vdupq_n_f32(kExp2P2), f, p);
    p = vfmaq_f32(vdupq_n_f32(kExp2P1), f, p);
    p = vfmaq_f32(vdupq_n_f32(kExp2P0), f, p);
    const float32x4_t tail = vfmaq_f32(vdupq_n_f32(1.0f), f, p);
    const int32x4_t ki = vcvtq_s32_f32(k);
    return vmulq_f32(tail, vreinterpretq_f32_s32(
                               vshlq_n_s32(vaddq_s32(ki, vdupq_n_s32(127)), 23)));
}

// Callers that can guarantee a strictly positive base skip the guard below.
inline float32x4_t vecPowPos(float32x4_t x, float32x4_t c) {
    return vecExp2(vmulq_f32(c, vecLog2(x)));
}

inline float32x4_t vecPow(float32x4_t x, float32x4_t c) {
    const float32x4_t out =
        vecPowPos(vmaxq_f32(x, vdupq_n_f32(1.17549435e-38f)), c);
    return vbslq_f32(vcleq_f32(x, vdupq_n_f32(0.0f)), vdupq_n_f32(0.0f), out);
}

void vecPowToLinear(const TransferOp &op, float *row, int w) {
    const float32x4_t c = vdupq_n_f32(op.p0);
    int i = 0;
    for (; i + 4 <= w; i += 4)
        vst1q_f32(row + i, vecPow(vld1q_f32(row + i), c));
    for (; i < w; i++)
        row[i] = powToLinear(row[i], op);
}

// The pow argument is affine in v and bounded at 0.090, so vecPowPos applies.
void vecSt170ToLinear(const TransferOp &op, float *row, int w) {
    const float32x4_t zero = vdupq_n_f32(0.0f), cut = vdupq_n_f32(0.0812f);
    const float32x4_t es = vdupq_n_f32(1.0f / 1.099f);
    const float32x4_t eo = vdupq_n_f32(0.099f / 1.099f);
    const float32x4_t e = vdupq_n_f32(1.0f / 0.45f);
    const float32x4_t ls = vdupq_n_f32(1.0f / 4.5f);
    int i = 0;
    for (; i + 4 <= w; i += 4) {
        const float32x4_t v = vmaxq_f32(vld1q_f32(row + i), zero);
        vst1q_f32(row + i, vbslq_f32(vcltq_f32(v, cut), vmulq_f32(v, ls),
                                     vecPowPos(vfmaq_f32(eo, v, es), e)));
    }
    for (; i < w; i++)
        row[i] = st170ToLinear(row[i], op);
}

void vecBt1886ToLinear(const TransferOp &op, float *row, int w) {
    const float32x4_t vb = vdupq_n_f32(op.p1), zero = vdupq_n_f32(0.0f);
    const float32x4_t va = vdupq_n_f32(op.p0), e = vdupq_n_f32(2.4f);
    int i = 0;
    for (; i + 4 <= w; i += 4) {
        const float32x4_t t = vmaxq_f32(vaddq_f32(vld1q_f32(row + i), vb), zero);
        vst1q_f32(row + i, vmulq_f32(va, vecPow(t, e)));
    }
    for (; i < w; i++)
        row[i] = bt1886ToLinear(row[i], op);
}

// The lower segment is cubed directly rather than routed through the
// polynomial: same cost, and five times the relative accuracy in the dark end
// where that segment lives.
void vecBt1886CrtToLinear(const TransferOp &op, float *row, int w) {
    const float32x4_t vb = vdupq_n_f32(op.p1), zero = vdupq_n_f32(0.0f);
    const float32x4_t vc = vdupq_n_f32(kBt1886Vc), e = vdupq_n_f32(2.6f);
    const float32x4_t s2 = vdupq_n_f32(op.p2), s0 = vdupq_n_f32(op.p0);
    int i = 0;
    for (; i + 4 <= w; i += 4) {
        const float32x4_t x = vld1q_f32(row + i);
        const float32x4_t t = vmaxq_f32(vaddq_f32(x, vb), zero);
        const float32x4_t cube = vmulq_f32(s2, vmulq_f32(t, vmulq_f32(t, t)));
        const float32x4_t hi = vmulq_f32(s0, vecPow(t, e));
        vst1q_f32(row + i, vbslq_f32(vcltq_f32(x, vc), cube, hi));
    }
    for (; i < w; i++)
        row[i] = bt1886CrtToLinear(row[i], op);
}

void vecSrgbToLinear(const TransferOp &op, float *row, int w) {
    const float32x4_t zero = vdupq_n_f32(0.0f), cut = vdupq_n_f32(0.04045f);
    const float32x4_t es = vdupq_n_f32(1.0f / 1.055f);
    const float32x4_t eo = vdupq_n_f32(0.055f / 1.055f);
    const float32x4_t e = vdupq_n_f32(2.4f), ls = vdupq_n_f32(1.0f / 12.92f);
    int i = 0;
    for (; i + 4 <= w; i += 4) {
        const float32x4_t v = vmaxq_f32(vld1q_f32(row + i), zero);
        vst1q_f32(row + i, vbslq_f32(vcleq_f32(v, cut), vmulq_f32(v, ls),
                                     vecPowPos(vfmaq_f32(eo, v, es), e)));
    }
    for (; i < w; i++)
        row[i] = srgbToLinear(row[i], op);
}

void vecBt1886FromLinear(const TransferOp &op, float *row, int w) {
    const float32x4_t c = vdupq_n_f32(1.0f / 2.4f), s = vdupq_n_f32(op.p0);
    const float32x4_t vb = vdupq_n_f32(op.p1), zero = vdupq_n_f32(0.0f);
    int i = 0;
    for (; i + 4 <= w; i += 4) {
        const float32x4_t t = vmulq_f32(vmaxq_f32(vld1q_f32(row + i), zero), s);
        vst1q_f32(row + i, vsubq_f32(vecPow(t, c), vb));
    }
    for (; i < w; i++)
        row[i] = bt1886FromLinear(row[i], op);
}

// Lanes below beta take the linear segment, so clamping the pow argument up to
// beta keeps vecPowPos usable without changing any result.
void vecBt2020OetfFromLinear(const TransferOp &op, float *row, int w) {
    const float32x4_t zero = vdupq_n_f32(0.0f), beta = vdupq_n_f32(kOetfBeta);
    const float32x4_t e = vdupq_n_f32(0.45f);
    const float32x4_t alpha = vdupq_n_f32(kOetfAlpha);
    const float32x4_t off = vdupq_n_f32(-(kOetfAlpha - 1.0f));
    int i = 0;
    for (; i + 4 <= w; i += 4) {
        const float32x4_t v = vmaxq_f32(vld1q_f32(row + i), zero);
        const float32x4_t p =
            vfmaq_f32(off, alpha, vecPowPos(vmaxq_f32(v, beta), e));
        vst1q_f32(row + i, vbslq_f32(vcltq_f32(v, beta),
                                     vmulq_f32(v, vdupq_n_f32(4.5f)), p));
    }
    for (; i < w; i++)
        row[i] = bt2020OetfFromLinear(row[i], op);
}

// Same treatment as the BT.2020 OETF above: the pow argument is clamped up to
// the breakpoint, whose lanes the select discards anyway.
void vecSrgbFromLinear(const TransferOp &op, float *row, int w) {
    const float32x4_t zero = vdupq_n_f32(0.0f), cut = vdupq_n_f32(kSrgbLinearCut);
    const float32x4_t e = vdupq_n_f32(1.0f / 2.4f), a = vdupq_n_f32(1.055f);
    const float32x4_t off = vdupq_n_f32(-0.055f), ls = vdupq_n_f32(12.92f);
    int i = 0;
    for (; i + 4 <= w; i += 4) {
        const float32x4_t v = vmaxq_f32(vld1q_f32(row + i), zero);
        const float32x4_t p = vfmaq_f32(off, a, vecPowPos(vmaxq_f32(v, cut), e));
        vst1q_f32(row + i, vbslq_f32(vcleq_f32(v, cut), vmulq_f32(v, ls), p));
    }
    for (; i < w; i++)
        row[i] = srgbFromLinear(row[i], op);
}

void vecPqFromLinear(const TransferOp &op, float *row, int w) {
    const float32x4_t sc = vdupq_n_f32(op.p0), zero = vdupq_n_f32(0.0f);
    const float32x4_t one = vdupq_n_f32(1.0f);
    const float32x4_t m1 = vdupq_n_f32(kPqM1), m2 = vdupq_n_f32(kPqM2);
    const float32x4_t c1 = vdupq_n_f32(kPqC1 - 1.0f);
    const float32x4_t c2 = vdupq_n_f32(kPqC2 - kPqC3), c3 = vdupq_n_f32(kPqC3);
    int i = 0;
    for (; i + 4 <= w; i += 4) {
        const float32x4_t x = vmaxq_f32(vmulq_f32(vld1q_f32(row + i), sc), zero);
        const float32x4_t xpow = vecPow(x, m1);
        const float32x4_t num = vfmaq_f32(c1, c2, xpow);
        const float32x4_t den = vfmaq_f32(one, c3, xpow);
        vst1q_f32(row + i, vecPowPos(vaddq_f32(one, vdivq_f32(num, den)), m2));
    }
    for (; i < w; i++)
        row[i] = pqFromLinear(row[i], op);
}

// log(t) is ln2 * log2(t), so the OETF's log segment reuses vecLog2 with the
// factor folded into the constant.
void vecHlgFromLinear(const TransferOp &op, float *row, int w) {
    const float32x4_t sc = vdupq_n_f32(op.p0), zero = vdupq_n_f32(0.0f);
    const float32x4_t ce = vdupq_n_f32(1.0f / 1.2f);
    const float32x4_t tiny = vdupq_n_f32(1.17549435e-38f);
    const float32x4_t aln2 = vdupq_n_f32(kHlgA * 0.69314718f);
    const float32x4_t vc = vdupq_n_f32(kHlgC), cut = vdupq_n_f32(1.0f / 12.0f);
    const float32x4_t nb = vdupq_n_f32(-kHlgB), twelve = vdupq_n_f32(12.0f);
    const float32x4_t three = vdupq_n_f32(3.0f);
    int i = 0;
    for (; i + 4 <= w; i += 4) {
        const float32x4_t x = vecPow(
            vmaxq_f32(vmulq_f32(vld1q_f32(row + i), sc), zero), ce);
        // Below the breakpoint 12x-b is negative; clamp so the log stays finite
        // and discard those lanes in the select.
        const float32x4_t t = vmaxq_f32(vfmaq_f32(nb, x, twelve), tiny);
        const float32x4_t hi = vfmaq_f32(vc, aln2, vecLog2(t));
        const float32x4_t lo = vsqrtq_f32(vmulq_f32(three, x));
        vst1q_f32(row + i, vbslq_f32(vcleq_f32(x, cut), lo, hi));
    }
    for (; i < w; i++)
        row[i] = hlgFromLinear(row[i], op);
}

#elif defined(MODERNIZE_X86)

// The AVX2 half, operation for operation the same as the NEON half above,
// eight lanes at a time. Every function carries the target attribute so the
// helpers can inline into the row kernels; applyTransferSimd stays untargeted
// and gates on the runtime check.

MODERNIZE_TARGET_AVX2
inline __m256 vecLog2(__m256 x) {
    const __m256i ix = _mm256_castps_si256(x);
    __m256i e = _mm256_sub_epi32(_mm256_srai_epi32(ix, 23), _mm256_set1_epi32(127));
    __m256 m = _mm256_castsi256_ps(
        _mm256_or_si256(_mm256_and_si256(ix, _mm256_set1_epi32(0x007FFFFF)),
                        _mm256_set1_epi32(0x3F800000)));
    const __m256 big = _mm256_cmp_ps(m, _mm256_set1_ps(1.41421356f), _CMP_GT_OQ);
    m = _mm256_blendv_ps(m, _mm256_mul_ps(m, _mm256_set1_ps(0.5f)), big);
    e = _mm256_add_epi32(e, _mm256_srli_epi32(_mm256_castps_si256(big), 31));
    const __m256 one = _mm256_set1_ps(1.0f);
    const __m256 f = _mm256_div_ps(_mm256_sub_ps(m, one), _mm256_add_ps(m, one));
    const __m256 s = _mm256_mul_ps(f, f);
    __m256 p = _mm256_set1_ps(kLog2P4);
    p = _mm256_fmadd_ps(p, s, _mm256_set1_ps(kLog2P3));
    p = _mm256_fmadd_ps(p, s, _mm256_set1_ps(kLog2P2));
    p = _mm256_fmadd_ps(p, s, _mm256_set1_ps(kLog2P1));
    p = _mm256_fmadd_ps(p, s, _mm256_set1_ps(kLog2P0));
    return _mm256_fmadd_ps(f, p, _mm256_cvtepi32_ps(e));
}

MODERNIZE_TARGET_AVX2
inline __m256 vecExp2(__m256 y) {
    y = _mm256_min_ps(_mm256_max_ps(y, _mm256_set1_ps(-127.0f)),
                      _mm256_set1_ps(128.0f));
    const __m256 k =
        _mm256_round_ps(y, _MM_FROUND_TO_NEAREST_INT | _MM_FROUND_NO_EXC);
    const __m256 f = _mm256_sub_ps(y, k);
    __m256 p = _mm256_set1_ps(kExp2P6);
    p = _mm256_fmadd_ps(p, f, _mm256_set1_ps(kExp2P5));
    p = _mm256_fmadd_ps(p, f, _mm256_set1_ps(kExp2P4));
    p = _mm256_fmadd_ps(p, f, _mm256_set1_ps(kExp2P3));
    p = _mm256_fmadd_ps(p, f, _mm256_set1_ps(kExp2P2));
    p = _mm256_fmadd_ps(p, f, _mm256_set1_ps(kExp2P1));
    p = _mm256_fmadd_ps(p, f, _mm256_set1_ps(kExp2P0));
    const __m256 tail = _mm256_fmadd_ps(p, f, _mm256_set1_ps(1.0f));
    const __m256i ki = _mm256_cvttps_epi32(k);
    return _mm256_mul_ps(
        tail, _mm256_castsi256_ps(
                  _mm256_slli_epi32(_mm256_add_epi32(ki, _mm256_set1_epi32(127)), 23)));
}

MODERNIZE_TARGET_AVX2
inline __m256 vecPowPos(__m256 x, __m256 c) {
    return vecExp2(_mm256_mul_ps(c, vecLog2(x)));
}

MODERNIZE_TARGET_AVX2
inline __m256 vecPow(__m256 x, __m256 c) {
    const __m256 out =
        vecPowPos(_mm256_max_ps(x, _mm256_set1_ps(1.17549435e-38f)), c);
    return _mm256_blendv_ps(
        out, _mm256_setzero_ps(),
        _mm256_cmp_ps(x, _mm256_setzero_ps(), _CMP_LE_OQ));
}

MODERNIZE_TARGET_AVX2
void vecPowToLinear(const TransferOp &op, float *row, int w) {
    const __m256 c = _mm256_set1_ps(op.p0);
    int i = 0;
    for (; i + 8 <= w; i += 8)
        _mm256_storeu_ps(row + i, vecPow(_mm256_loadu_ps(row + i), c));
    for (; i < w; i++)
        row[i] = powToLinear(row[i], op);
}

MODERNIZE_TARGET_AVX2
void vecSt170ToLinear(const TransferOp &op, float *row, int w) {
    const __m256 zero = _mm256_setzero_ps(), cut = _mm256_set1_ps(0.0812f);
    const __m256 es = _mm256_set1_ps(1.0f / 1.099f);
    const __m256 eo = _mm256_set1_ps(0.099f / 1.099f);
    const __m256 e = _mm256_set1_ps(1.0f / 0.45f);
    const __m256 ls = _mm256_set1_ps(1.0f / 4.5f);
    int i = 0;
    for (; i + 8 <= w; i += 8) {
        const __m256 v = _mm256_max_ps(_mm256_loadu_ps(row + i), zero);
        const __m256 hi = vecPowPos(_mm256_fmadd_ps(v, es, eo), e);
        _mm256_storeu_ps(row + i,
                         _mm256_blendv_ps(hi, _mm256_mul_ps(v, ls),
                                          _mm256_cmp_ps(v, cut, _CMP_LT_OQ)));
    }
    for (; i < w; i++)
        row[i] = st170ToLinear(row[i], op);
}

MODERNIZE_TARGET_AVX2
void vecBt1886ToLinear(const TransferOp &op, float *row, int w) {
    const __m256 vb = _mm256_set1_ps(op.p1), zero = _mm256_setzero_ps();
    const __m256 va = _mm256_set1_ps(op.p0), e = _mm256_set1_ps(2.4f);
    int i = 0;
    for (; i + 8 <= w; i += 8) {
        const __m256 t =
            _mm256_max_ps(_mm256_add_ps(_mm256_loadu_ps(row + i), vb), zero);
        _mm256_storeu_ps(row + i, _mm256_mul_ps(va, vecPow(t, e)));
    }
    for (; i < w; i++)
        row[i] = bt1886ToLinear(row[i], op);
}

MODERNIZE_TARGET_AVX2
void vecBt1886CrtToLinear(const TransferOp &op, float *row, int w) {
    const __m256 vb = _mm256_set1_ps(op.p1), zero = _mm256_setzero_ps();
    const __m256 vc = _mm256_set1_ps(kBt1886Vc), e = _mm256_set1_ps(2.6f);
    const __m256 s2 = _mm256_set1_ps(op.p2), s0 = _mm256_set1_ps(op.p0);
    int i = 0;
    for (; i + 8 <= w; i += 8) {
        const __m256 x = _mm256_loadu_ps(row + i);
        const __m256 t = _mm256_max_ps(_mm256_add_ps(x, vb), zero);
        const __m256 cube =
            _mm256_mul_ps(s2, _mm256_mul_ps(t, _mm256_mul_ps(t, t)));
        const __m256 hi = _mm256_mul_ps(s0, vecPow(t, e));
        _mm256_storeu_ps(row + i,
                         _mm256_blendv_ps(hi, cube,
                                          _mm256_cmp_ps(x, vc, _CMP_LT_OQ)));
    }
    for (; i < w; i++)
        row[i] = bt1886CrtToLinear(row[i], op);
}

MODERNIZE_TARGET_AVX2
void vecSrgbToLinear(const TransferOp &op, float *row, int w) {
    const __m256 zero = _mm256_setzero_ps(), cut = _mm256_set1_ps(0.04045f);
    const __m256 es = _mm256_set1_ps(1.0f / 1.055f);
    const __m256 eo = _mm256_set1_ps(0.055f / 1.055f);
    const __m256 e = _mm256_set1_ps(2.4f), ls = _mm256_set1_ps(1.0f / 12.92f);
    int i = 0;
    for (; i + 8 <= w; i += 8) {
        const __m256 v = _mm256_max_ps(_mm256_loadu_ps(row + i), zero);
        const __m256 hi = vecPowPos(_mm256_fmadd_ps(v, es, eo), e);
        _mm256_storeu_ps(row + i,
                         _mm256_blendv_ps(hi, _mm256_mul_ps(v, ls),
                                          _mm256_cmp_ps(v, cut, _CMP_LE_OQ)));
    }
    for (; i < w; i++)
        row[i] = srgbToLinear(row[i], op);
}

MODERNIZE_TARGET_AVX2
void vecBt1886FromLinear(const TransferOp &op, float *row, int w) {
    const __m256 c = _mm256_set1_ps(1.0f / 2.4f), s = _mm256_set1_ps(op.p0);
    const __m256 vb = _mm256_set1_ps(op.p1), zero = _mm256_setzero_ps();
    int i = 0;
    for (; i + 8 <= w; i += 8) {
        const __m256 t = _mm256_mul_ps(
            _mm256_max_ps(_mm256_loadu_ps(row + i), zero), s);
        _mm256_storeu_ps(row + i, _mm256_sub_ps(vecPow(t, c), vb));
    }
    for (; i < w; i++)
        row[i] = bt1886FromLinear(row[i], op);
}

MODERNIZE_TARGET_AVX2
void vecBt2020OetfFromLinear(const TransferOp &op, float *row, int w) {
    const __m256 zero = _mm256_setzero_ps(), beta = _mm256_set1_ps(kOetfBeta);
    const __m256 e = _mm256_set1_ps(0.45f);
    const __m256 alpha = _mm256_set1_ps(kOetfAlpha);
    const __m256 off = _mm256_set1_ps(-(kOetfAlpha - 1.0f));
    const __m256 slope = _mm256_set1_ps(4.5f);
    int i = 0;
    for (; i + 8 <= w; i += 8) {
        const __m256 v = _mm256_max_ps(_mm256_loadu_ps(row + i), zero);
        const __m256 p = _mm256_fmadd_ps(
            alpha, vecPowPos(_mm256_max_ps(v, beta), e), off);
        _mm256_storeu_ps(row + i,
                         _mm256_blendv_ps(p, _mm256_mul_ps(v, slope),
                                          _mm256_cmp_ps(v, beta, _CMP_LT_OQ)));
    }
    for (; i < w; i++)
        row[i] = bt2020OetfFromLinear(row[i], op);
}

MODERNIZE_TARGET_AVX2
void vecSrgbFromLinear(const TransferOp &op, float *row, int w) {
    const __m256 zero = _mm256_setzero_ps(), cut = _mm256_set1_ps(kSrgbLinearCut);
    const __m256 e = _mm256_set1_ps(1.0f / 2.4f), a = _mm256_set1_ps(1.055f);
    const __m256 off = _mm256_set1_ps(-0.055f), ls = _mm256_set1_ps(12.92f);
    int i = 0;
    for (; i + 8 <= w; i += 8) {
        const __m256 v = _mm256_max_ps(_mm256_loadu_ps(row + i), zero);
        const __m256 p =
            _mm256_fmadd_ps(a, vecPowPos(_mm256_max_ps(v, cut), e), off);
        _mm256_storeu_ps(row + i,
                         _mm256_blendv_ps(p, _mm256_mul_ps(v, ls),
                                          _mm256_cmp_ps(v, cut, _CMP_LE_OQ)));
    }
    for (; i < w; i++)
        row[i] = srgbFromLinear(row[i], op);
}

MODERNIZE_TARGET_AVX2
void vecPqFromLinear(const TransferOp &op, float *row, int w) {
    const __m256 sc = _mm256_set1_ps(op.p0), zero = _mm256_setzero_ps();
    const __m256 one = _mm256_set1_ps(1.0f);
    const __m256 m1 = _mm256_set1_ps(kPqM1), m2 = _mm256_set1_ps(kPqM2);
    const __m256 c1 = _mm256_set1_ps(kPqC1 - 1.0f);
    const __m256 c2 = _mm256_set1_ps(kPqC2 - kPqC3), c3 = _mm256_set1_ps(kPqC3);
    int i = 0;
    for (; i + 8 <= w; i += 8) {
        const __m256 x =
            _mm256_max_ps(_mm256_mul_ps(_mm256_loadu_ps(row + i), sc), zero);
        const __m256 xpow = vecPow(x, m1);
        const __m256 num = _mm256_fmadd_ps(c2, xpow, c1);
        const __m256 den = _mm256_fmadd_ps(c3, xpow, one);
        _mm256_storeu_ps(
            row + i,
            vecPowPos(_mm256_add_ps(one, _mm256_div_ps(num, den)), m2));
    }
    for (; i < w; i++)
        row[i] = pqFromLinear(row[i], op);
}

MODERNIZE_TARGET_AVX2
void vecHlgFromLinear(const TransferOp &op, float *row, int w) {
    const __m256 sc = _mm256_set1_ps(op.p0), zero = _mm256_setzero_ps();
    const __m256 ce = _mm256_set1_ps(1.0f / 1.2f);
    const __m256 tiny = _mm256_set1_ps(1.17549435e-38f);
    const __m256 aln2 = _mm256_set1_ps(kHlgA * 0.69314718f);
    const __m256 vc = _mm256_set1_ps(kHlgC), cut = _mm256_set1_ps(1.0f / 12.0f);
    const __m256 nb = _mm256_set1_ps(-kHlgB), twelve = _mm256_set1_ps(12.0f);
    const __m256 three = _mm256_set1_ps(3.0f);
    int i = 0;
    for (; i + 8 <= w; i += 8) {
        const __m256 x = vecPow(
            _mm256_max_ps(_mm256_mul_ps(_mm256_loadu_ps(row + i), sc), zero), ce);
        const __m256 t = _mm256_max_ps(_mm256_fmadd_ps(x, twelve, nb), tiny);
        const __m256 hi = _mm256_fmadd_ps(aln2, vecLog2(t), vc);
        const __m256 lo = _mm256_sqrt_ps(_mm256_mul_ps(three, x));
        _mm256_storeu_ps(row + i,
                         _mm256_blendv_ps(hi, lo,
                                          _mm256_cmp_ps(x, cut, _CMP_LE_OQ)));
    }
    for (; i < w; i++)
        row[i] = hlgFromLinear(row[i], op);
}

const bool kHasAvx2Transfer = cpuHasAvx2Fma();

#endif  // MODERNIZE_ARM64 / MODERNIZE_X86

// Returns false for kinds with no vector kernel, leaving them to the scalar
// switch below.
bool applyTransferSimd(const TransferOp &op, float *row, int w) {
#if defined(MODERNIZE_X86)
    if (!kHasAvx2Transfer)
        return false;
#endif
    switch (op.kind) {
    case TransferOp::Kind::PowToLinear: vecPowToLinear(op, row, w); return true;
    case TransferOp::Kind::St170ToLinear: vecSt170ToLinear(op, row, w); return true;
    case TransferOp::Kind::Bt1886ToLinear: vecBt1886ToLinear(op, row, w); return true;
    case TransferOp::Kind::Bt1886CrtToLinear:
        vecBt1886CrtToLinear(op, row, w);
        return true;
    case TransferOp::Kind::SrgbToLinear: vecSrgbToLinear(op, row, w); return true;
    case TransferOp::Kind::Bt1886FromLinear:
        vecBt1886FromLinear(op, row, w);
        return true;
    case TransferOp::Kind::Bt2020OetfFromLinear:
        vecBt2020OetfFromLinear(op, row, w);
        return true;
    case TransferOp::Kind::SrgbFromLinear: vecSrgbFromLinear(op, row, w); return true;
    case TransferOp::Kind::PqFromLinear: vecPqFromLinear(op, row, w); return true;
    case TransferOp::Kind::HlgFromLinear: vecHlgFromLinear(op, row, w); return true;
    case TransferOp::Kind::Identity: return false;
    }
    return false;
}

#endif  // MODERNIZE_ARM64 || MODERNIZE_X86

void applyTransfer(const TransferOp &op, float *row, int w) {
#if defined(MODERNIZE_ARM64) || defined(MODERNIZE_X86)
    if (applyTransferSimd(op, row, w))
        return;
#endif
    auto run = [&](float (*fn)(float, const TransferOp &)) {
        for (int i = 0; i < w; i++)
            row[i] = fn(row[i], op);
    };
    switch (op.kind) {
    case TransferOp::Kind::Identity: break;
    case TransferOp::Kind::PowToLinear: run(powToLinear); break;
    case TransferOp::Kind::St170ToLinear: run(st170ToLinear); break;
    case TransferOp::Kind::Bt1886ToLinear: run(bt1886ToLinear); break;
    case TransferOp::Kind::Bt1886CrtToLinear: run(bt1886CrtToLinear); break;
    case TransferOp::Kind::SrgbToLinear: run(srgbToLinear); break;
    case TransferOp::Kind::Bt1886FromLinear: run(bt1886FromLinear); break;
    case TransferOp::Kind::Bt2020OetfFromLinear: run(bt2020OetfFromLinear); break;
    case TransferOp::Kind::SrgbFromLinear: run(srgbFromLinear); break;
    case TransferOp::Kind::PqFromLinear: run(pqFromLinear); break;
    case TransferOp::Kind::HlgFromLinear: run(hlgFromLinear); break;
    }
}

// =====================
// Affine (3x3 + offset) row kernel, with runtime SIMD dispatch
// =====================

struct Affine {
    float m[3][3];
    float o[3];
};

using AffineFn = void (*)(const Affine &, float *, float *, float *, int);

void affineRowsScalar(const Affine &a, float *r, float *g, float *b, int w) {
    for (int i = 0; i < w; i++) {
        const float x = r[i], y = g[i], z = b[i];
        r[i] = a.m[0][0] * x + a.m[0][1] * y + a.m[0][2] * z + a.o[0];
        g[i] = a.m[1][0] * x + a.m[1][1] * y + a.m[1][2] * z + a.o[1];
        b[i] = a.m[2][0] * x + a.m[2][1] * y + a.m[2][2] * z + a.o[2];
    }
}

#if defined(MODERNIZE_X86)

MODERNIZE_TARGET_AVX2
void affineRowsAvx2(const Affine &a, float *r, float *g, float *b, int w) {
    const __m256 m00 = _mm256_set1_ps(a.m[0][0]), m01 = _mm256_set1_ps(a.m[0][1]),
                 m02 = _mm256_set1_ps(a.m[0][2]), m10 = _mm256_set1_ps(a.m[1][0]),
                 m11 = _mm256_set1_ps(a.m[1][1]), m12 = _mm256_set1_ps(a.m[1][2]),
                 m20 = _mm256_set1_ps(a.m[2][0]), m21 = _mm256_set1_ps(a.m[2][1]),
                 m22 = _mm256_set1_ps(a.m[2][2]), o0 = _mm256_set1_ps(a.o[0]),
                 o1 = _mm256_set1_ps(a.o[1]), o2 = _mm256_set1_ps(a.o[2]);
    int i = 0;
    for (; i + 8 <= w; i += 8) {
        const __m256 x = _mm256_loadu_ps(r + i);
        const __m256 y = _mm256_loadu_ps(g + i);
        const __m256 z = _mm256_loadu_ps(b + i);
        __m256 rr = _mm256_fmadd_ps(m00, x, o0);
        rr = _mm256_fmadd_ps(m01, y, rr);
        rr = _mm256_fmadd_ps(m02, z, rr);
        __m256 gg = _mm256_fmadd_ps(m10, x, o1);
        gg = _mm256_fmadd_ps(m11, y, gg);
        gg = _mm256_fmadd_ps(m12, z, gg);
        __m256 bb = _mm256_fmadd_ps(m20, x, o2);
        bb = _mm256_fmadd_ps(m21, y, bb);
        bb = _mm256_fmadd_ps(m22, z, bb);
        _mm256_storeu_ps(r + i, rr);
        _mm256_storeu_ps(g + i, gg);
        _mm256_storeu_ps(b + i, bb);
    }
    if (i < w)
        affineRowsScalar(a, r + i, g + i, b + i, w - i);
}

#elif defined(MODERNIZE_ARM64)

// The coefficients are broadcast into registers up front on purpose: the
// scalar-operand form (vfmaq_n_f32) makes clang reload all nine from memory
// on every iteration, which costs more than the arithmetic.
void affineRowsNeon(const Affine &a, float *r, float *g, float *b, int w) {
    const float32x4_t m00 = vdupq_n_f32(a.m[0][0]), m01 = vdupq_n_f32(a.m[0][1]),
                      m02 = vdupq_n_f32(a.m[0][2]), m10 = vdupq_n_f32(a.m[1][0]),
                      m11 = vdupq_n_f32(a.m[1][1]), m12 = vdupq_n_f32(a.m[1][2]),
                      m20 = vdupq_n_f32(a.m[2][0]), m21 = vdupq_n_f32(a.m[2][1]),
                      m22 = vdupq_n_f32(a.m[2][2]), o0 = vdupq_n_f32(a.o[0]),
                      o1 = vdupq_n_f32(a.o[1]), o2 = vdupq_n_f32(a.o[2]);
    int i = 0;
    for (; i + 4 <= w; i += 4) {
        const float32x4_t x = vld1q_f32(r + i);
        const float32x4_t y = vld1q_f32(g + i);
        const float32x4_t z = vld1q_f32(b + i);
        float32x4_t rr = vfmaq_f32(o0, x, m00);
        rr = vfmaq_f32(rr, y, m01);
        rr = vfmaq_f32(rr, z, m02);
        float32x4_t gg = vfmaq_f32(o1, x, m10);
        gg = vfmaq_f32(gg, y, m11);
        gg = vfmaq_f32(gg, z, m12);
        float32x4_t bb = vfmaq_f32(o2, x, m20);
        bb = vfmaq_f32(bb, y, m21);
        bb = vfmaq_f32(bb, z, m22);
        vst1q_f32(r + i, rr);
        vst1q_f32(g + i, gg);
        vst1q_f32(b + i, bb);
    }
    if (i < w)
        affineRowsScalar(a, r + i, g + i, b + i, w - i);
}

#endif

AffineFn selectAffineKernel() {
#if defined(MODERNIZE_X86)
    return cpuHasAvx2Fma() ? affineRowsAvx2 : affineRowsScalar;
#elif defined(MODERNIZE_ARM64)
    return affineRowsNeon;
#else
    return affineRowsScalar;
#endif
}

const AffineFn kAffineRows = selectAffineKernel();

// =====================
// Constant luminance encode
// =====================
//
// Constant luminance is not a transfer characteristic: it changes where the
// curve is applied. NCL encodes each channel and then matrixes
// (Y' = K_R R' + K_G G' + K_B B'); CL matrixes the linear tristimulus and
// encodes the result (Y'c = (K_R R + K_G G + K_B B)'), so luminance survives
// chroma subsampling intact. H.273 Equations E-56 to E-65 (shared by
// matrix_coeffs 10 and 13) define the color-difference normalizers against
// whichever transfer characteristic is signalled — N_B = (1-K_B)',
// P_B = 1-(K_B)', likewise for R — so they are derived here from the output
// transfer rather than pinned to any one curve. BT.2020 Table 4's published
// numbers are that derivation under BT.2020's own OETF, which stays the
// default when no output transfer is named.

struct ClParams {
    // H.273 E-62..E-65. N_x is stored by magnitude, so both branches divide
    // by 2x, matching the standard's -2N_x / 2P_x denominators.
    float nb, pb, nr, pr;
    float kr, kg, kb;
};

ClParams makeClParams(const TransferOp &toGamma, double kr, double kb) {
    ClParams p;
    p.kr = static_cast<float>(kr);
    p.kb = static_cast<float>(kb);
    p.kg = static_cast<float>(1.0 - kr - kb);
    p.nb = transferSample(toGamma, static_cast<float>(1.0 - kb));
    p.pb = 1.0f - transferSample(toGamma, static_cast<float>(kb));
    p.nr = transferSample(toGamma, static_cast<float>(1.0 - kr));
    p.pr = 1.0f - transferSample(toGamma, static_cast<float>(kr));
    return p;
}

// Linear RGB rows -> Y'cCbcCrc rows in place.
//
// The transfer is applied to three whole rows rather than per sample, so the
// curve is selected once instead of 3*w times and each row goes through
// whatever kernel applyTransfer picks. Luminance is staged in g, which is dead
// the moment its own contribution is summed.
void applyConstantLuminance(const ClParams &p, const TransferOp &toGamma,
                            float *r, float *g, float *b, int w) {
    for (int i = 0; i < w; i++)
        g[i] = p.kr * r[i] + p.kg * g[i] + p.kb * b[i];
    applyTransfer(toGamma, g, w);
    applyTransfer(toGamma, b, w);
    applyTransfer(toGamma, r, w);
    // Reciprocals hoisted: the standard's -2N_x / 2P_x denominators are fixed
    // per pipeline, so the per-sample divide is a multiply.
    const float inb = 1.0f / (2.0f * p.nb), ipb = 1.0f / (2.0f * p.pb);
    const float inr = 1.0f / (2.0f * p.nr), ipr = 1.0f / (2.0f * p.pr);
    for (int i = 0; i < w; i++) {
        const float y = g[i];
        const float bMinusY = b[i] - y;
        const float rMinusY = r[i] - y;
        r[i] = y;
        g[i] = bMinusY * (bMinusY < 0.0f ? inb : ipb);
        b[i] = rMinusY * (rMinusY < 0.0f ? inr : ipr);
    }
}

// =====================
// String option parsing
// =====================

std::string toLower(std::string s) {
    std::transform(s.begin(), s.end(), s.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return s;
}

template <typename T>
struct Alias { const char *name; T value; };

constexpr Alias<InPrimaries> kInPrimariesAliases[] = {
    {"ntsc-1953", InPrimaries::Ntsc1953}, {"bt470m", InPrimaries::Ntsc1953},
    {"470m", InPrimaries::Ntsc1953}, {"fcc", InPrimaries::Ntsc1953},
    {"bt470-japan", InPrimaries::Bt470Japan}, {"470m93", InPrimaries::Bt470Japan},
    {"ntscj", InPrimaries::Bt470Japan},
    {"bt1700-japan", InPrimaries::Bt1700Japan}, {"170j", InPrimaries::Bt1700Japan},
    {"pal", InPrimaries::Pal}, {"ebu", InPrimaries::Pal},
    {"bbc", InPrimaries::Pal}, {"470bg", InPrimaries::Pal},
    {"smpte-c", InPrimaries::SmpteC}, {"st170", InPrimaries::SmpteC},
    {"170m", InPrimaries::SmpteC},
    {"code-point-22", InPrimaries::CodePoint22},
    {"ecia-xxa", InPrimaries::EciaXXA}, {"p22", InPrimaries::EciaXXA},
    {"ecia-xxb", InPrimaries::EciaXXB},
    {"ecia-xxc", InPrimaries::EciaXXC},
    {"ecia-xxd", InPrimaries::EciaXXD},
    {"ecia-xxe", InPrimaries::EciaXXE},
    {"ecia-xxf", InPrimaries::EciaXXF},
    {"ecia-xxg", InPrimaries::EciaXXG},
    {"rca-sulfide-8500k", InPrimaries::RcaSulfide8500K},
    {"rca-sulfide-9300k-27mpcd", InPrimaries::RcaSulfide9300K27MPCD},
    {"rca-sulfide-c", InPrimaries::RcaSulfideC},
    {"rca-p22-4-67", InPrimaries::RcaP22_4_67},
    {"rca-p22-5-61", InPrimaries::RcaP22_5_61},
    {"rca-p22-9-65", InPrimaries::RcaP22_9_65},
    {"sony-p22", InPrimaries::SonyP22},
    {"studio-japan", InPrimaries::StudioJapan},
    {"nederland-proposal", InPrimaries::NederlandProposal},
};

constexpr Alias<InTransfer> kInTransferAliases[] = {
    {"linear", InTransfer::Linear},
    {"ntsc-1953", InTransfer::Gamma22}, {"bt470m", InTransfer::Gamma22},
    {"470m", InTransfer::Gamma22}, {"fcc", InTransfer::Gamma22},
    {"gamma22", InTransfer::Gamma22},
    {"bt470bg", InTransfer::Gamma28}, {"470bg", InTransfer::Gamma28},
    {"tube", InTransfer::Gamma28}, {"gamma28", InTransfer::Gamma28},
    {"st170-scene", InTransfer::St170Scene}, {"st170-oetf", InTransfer::St170Scene},
    {"bt601", InTransfer::St170Scene}, {"601", InTransfer::St170Scene},
    {"st170-display", InTransfer::St170Display}, {"st170-eotf", InTransfer::St170Display},
    {"bt1886-annex-1", InTransfer::Bt1886Annex1}, {"1886", InTransfer::Bt1886Annex1},
    {"lcd", InTransfer::Bt1886Annex1}, {"gamma24", InTransfer::Bt1886Annex1},
    {"bt1886-appendix-1", InTransfer::Bt1886Appendix1}, {"1886a", InTransfer::Bt1886Appendix1},
    {"crt", InTransfer::Bt1886Appendix1},
    {"srgb", InTransfer::Srgb}, {"iec-61966-2-1", InTransfer::Srgb},
};

constexpr Alias<InMatrix> kInMatrixAliases[] = {
    {"analog-classic", InMatrix::AnalogClassic}, {"ntsc-1953", InMatrix::AnalogClassic},
    {"fcc", InMatrix::AnalogClassic},
    {"analog-modern", InMatrix::AnalogModern}, {"bt470", InMatrix::AnalogModern},
    {"bt1700", InMatrix::AnalogModern}, {"st170", InMatrix::AnalogModern},
    {"170m", InMatrix::AnalogModern}, {"bt601", InMatrix::AnalogModern},
    {"601", InMatrix::AnalogModern},
};

constexpr Alias<OutPrimaries> kOutPrimariesAliases[] = {
    {"bt709", OutPrimaries::Bt709}, {"709", OutPrimaries::Bt709},
    {"bt2020", OutPrimaries::Bt2020}, {"2020", OutPrimaries::Bt2020},
    {"p3dci", OutPrimaries::P3Dci}, {"st431-2", OutPrimaries::P3Dci},
    {"p3d65", OutPrimaries::P3D65}, {"st432-1", OutPrimaries::P3D65},
    {"xyz", OutPrimaries::Xyz}, {"st428", OutPrimaries::Xyz},
};

constexpr Alias<OutTransfer> kOutTransferAliases[] = {
    {"linear", OutTransfer::Linear},
    {"bt1886-annex-1", OutTransfer::Bt1886Annex1}, {"1886", OutTransfer::Bt1886Annex1},
    {"lcd", OutTransfer::Bt1886Annex1}, {"gamma24", OutTransfer::Bt1886Annex1},
    {"srgb", OutTransfer::Srgb}, {"iec-61966-2-1", OutTransfer::Srgb},
    {"pq", OutTransfer::Pq}, {"st2084", OutTransfer::Pq}, {"2084", OutTransfer::Pq},
    {"hlg", OutTransfer::Hlg}, {"std-b67", OutTransfer::Hlg},
};

constexpr Alias<OutMatrix> kOutMatrixAliases[] = {
    {"rgb", OutMatrix::Rgb},
    {"bt709", OutMatrix::Bt709}, {"709", OutMatrix::Bt709},
    {"bt2020ncl", OutMatrix::Bt2020Ncl}, {"bt2100", OutMatrix::Bt2020Ncl},
    {"2020ncl", OutMatrix::Bt2020Ncl}, {"2020", OutMatrix::Bt2020Ncl},
    {"2100", OutMatrix::Bt2020Ncl},
    {"2020cl", OutMatrix::Bt2020Cl},
    {"chromacl", OutMatrix::ChromaticityDerivedCl},
    {"chromaticity-derived-cl", OutMatrix::ChromaticityDerivedCl},
};

enum class OutputPreset { Hdtv, Bt2100Pq, Bt2100Hlg, Bt2020Sdr, Srgb };

constexpr Alias<OutputPreset> kOutputPresetAliases[] = {
    {"hdtv", OutputPreset::Hdtv}, {"bt709", OutputPreset::Hdtv},
    {"uhdtv", OutputPreset::Bt2100Pq}, {"bt2100-pq", OutputPreset::Bt2100Pq},
    {"bt2100-hlg", OutputPreset::Bt2100Hlg},
    {"bt2020-sdr", OutputPreset::Bt2020Sdr},
    {"srgb", OutputPreset::Srgb}, {"iec-61966-2-1", OutputPreset::Srgb},
};

template <typename T, size_t N>
std::optional<T> parseAlias(const Alias<T> (&table)[N], const std::string &value) {
    for (const auto &entry : table) {
        if (value == entry.name)
            return entry.value;
    }
    return std::nullopt;
}

template <typename T, size_t N>
std::string aliasNames(const Alias<T> (&table)[N]) {
    std::string names;
    for (const auto &entry : table) {
        if (!names.empty())
            names += ", ";
        names += entry.name;
    }
    return names;
}

// =====================
// Filter instance
// =====================

struct ModernizeData {
    VSNode *node = nullptr;
    VSVideoInfo vi = {};    // Output
    VSVideoInfo inVi = {};

    // Optional input-side overrides; absent values are inferred per frame from
    // frame properties.
    std::optional<InPrimaries> primariesIn;
    std::optional<InTransfer> transferIn;
    std::optional<InMatrix> matrixIn;
    std::optional<double> contrastIn;
    std::optional<double> brightnessIn;

    // Output side, fixed at creation.
    OutPrimaries primariesOut = OutPrimaries::Bt709;
    OutTransfer transferOut = OutTransfer::Bt1886Annex1;
    OutMatrix matrixOut = OutMatrix::Bt709;
    double contrastOut = 1.0;
    double brightnessOut = 0.0;
    double nominalLuminance = 100.0;
    bool chromaticAdaptation = false;

    int outMatrixTag = 1;
    int outTransferTag = 1;
    int outPrimariesTag = 1;

    // Set for the subsampled round trip, where the trailing chroma downsample
    // re-sites against the original _ChromaLocation.
    bool preserveChromaLocation = false;
};

struct Pipeline {
    Affine pre;         // Range normalization folded with Y'CbCr -> R'G'B'
    TransferOp toLinear;
    Affine gamut;       // Linear RGB in -> (CAT) -> linear RGB / XYZ out
    bool useCl = false;
    ClParams cl;
    TransferOp toGamma;
    Affine post;        // R'G'B' -> Y'CbCr folded with range denormalization
};

// Per-plane (v - offset) / scale normalization parameters.
struct PlaneScaling { double scale[3]; double offset[3]; };

PlaneScaling normalization(const VSVideoFormat &f, bool limited) {
    PlaneScaling n;
    if (f.sampleType == stFloat) {
        for (int p = 0; p < 3; p++) {
            n.scale[p] = 1.0;
            n.offset[p] = 0.0;
        }
        return n;
    }
    const int shift = f.bitsPerSample - 8;
    const bool yuv = f.colorFamily == cfYUV;
    for (int p = 0; p < 3; p++) {
        const bool chroma = yuv && p > 0;
        if (limited) {
            n.scale[p] = static_cast<double>((chroma ? 224 : 219) << shift);
            n.offset[p] = static_cast<double>((chroma ? 128 : 16) << shift);
        } else {
            n.scale[p] = static_cast<double>((1 << f.bitsPerSample) - 1);
            n.offset[p] = chroma ? static_cast<double>(1 << (f.bitsPerSample - 1)) : 0.0;
        }
    }
    return n;
}

// BT.1886 Annex 1 a/b from normalized white and black luminance.
void bt1886AnnexParams(double lw, double lb, double &a, double &b) {
    const double lwPow = std::pow(lw, 1.0 / 2.4);
    const double lbPow = std::pow(lb, 1.0 / 2.4);
    a = std::pow(lwPow - lbPow, 2.4);
    b = lbPow / (lwPow - lbPow);
}

std::optional<int64_t> getProp(const VSMap *props, const char *key, const VSAPI *vsapi) {
    int err;
    const int64_t v = vsapi->mapGetInt(props, key, 0, &err);
    if (err)
        return std::nullopt;
    return v;
}

// Build the per-frame pipeline from resolved parameters + frame properties.
// Returns false with a message when a needed property is absent or unsupported.
bool buildPipeline(const ModernizeData *d, const VSMap *props, Pipeline &pipe,
                   std::string &err, const VSAPI *vsapi) {
    const bool inIsYuv = d->inVi.format.colorFamily == cfYUV;

    // --- Input primaries ---
    InPrimaries inPrim;
    if (d->primariesIn) {
        inPrim = *d->primariesIn;
    } else {
        const auto tag = getProp(props, "_Primaries", vsapi);
        if (!tag || *tag == 2) {
            err = "input primaries unknown: no usable _Primaries frame property; "
                  "set primaries_in_s";
            return false;
        }
        switch (*tag) {
        case 4: inPrim = InPrimaries::Ntsc1953; break;
        case 5: inPrim = InPrimaries::Pal; break;
        case 6: inPrim = InPrimaries::SmpteC; break;
        case 7: inPrim = InPrimaries::SmpteC; break;  // ST 240 shares RP 145 chromaticity
        case 22: inPrim = InPrimaries::CodePoint22; break;
        default:
            err = "unsupported _Primaries frame property value " + std::to_string(*tag) +
                  "; set primaries_in_s to override";
            return false;
        }
    }

    // --- Input transfer ---
    InTransfer inTransfer;
    if (d->transferIn) {
        inTransfer = *d->transferIn;
    } else {
        const auto tag = getProp(props, "_Transfer", vsapi);
        if (!tag || *tag == 2) {
            err = "input transfer unknown: no usable _Transfer frame property; "
                  "set transfer_in_s";
            return false;
        }
        switch (*tag) {
        // BT.709's reference EOTF is BT.1886. ST 170 (6) gets the same
        // reading: its §5.2 inverse-OETF reproducer saw little use in real
        // displays, which were CRTs — the response BT.1886 Annex 1 was later
        // written to let flat panels emulate. resize/zimg agree (code 6 ≡
        // code 1, display-referred). Override with
        // transfer_in_s="st170-display" for a literal §5.2 display, or
        // "crt" (Appendix 1) for a closer CRT match.
        case 1: case 6: inTransfer = InTransfer::Bt1886Annex1; break;
        case 4: inTransfer = InTransfer::Gamma22; break;
        case 5: inTransfer = InTransfer::Gamma28; break;
        case 8: inTransfer = InTransfer::Linear; break;
        case 13: inTransfer = InTransfer::Srgb; break;
        default:
            err = "unsupported _Transfer frame property value " + std::to_string(*tag) +
                  "; set transfer_in_s to override";
            return false;
        }
    }

    const bool transferTakesContrast = inTransfer == InTransfer::Bt1886Annex1 ||
                                       inTransfer == InTransfer::Bt1886Appendix1;
    if ((d->contrastIn || d->brightnessIn) && !transferTakesContrast) {
        err = "contrast_in/brightness_in only apply to the bt1886-annex-1 and "
              "bt1886-appendix-1 input transfers";
        return false;
    }

    // --- Input matrix (Y'CbCr input only) ---
    InMatrix inMatrix = InMatrix::AnalogModern;
    if (inIsYuv) {
        if (d->matrixIn) {
            inMatrix = *d->matrixIn;
        } else {
            const auto tag = getProp(props, "_Matrix", vsapi);
            if (!tag || *tag == 2) {
                err = "input matrix unknown: no usable _Matrix frame property; "
                      "set matrix_in_s";
                return false;
            }
            switch (*tag) {
            case 4: inMatrix = InMatrix::AnalogClassic; break;
            case 5: case 6: inMatrix = InMatrix::AnalogModern; break;
            default:
                err = "unsupported _Matrix frame property value " + std::to_string(*tag) +
                      "; set matrix_in_s to override";
                return false;
            }
        }
    }

    // --- Input range ---
    bool inLimited = inIsYuv;  // Default: YUV studio range, RGB full range
    if (const auto colorRange = getProp(props, "_ColorRange", vsapi))
        inLimited = *colorRange == 1;
    else if (const auto legacyRange = getProp(props, "_Range", vsapi))
        inLimited = *legacyRange == 0;  // _Range's polarity is inverted from _ColorRange's

    // --- Pre: normalize + Y'CbCr -> R'G'B' ---
    const PlaneScaling inNorm = normalization(d->inVi.format, inLimited);
    Mat3 decode = kIdentity;
    if (inIsYuv) {
        decode = inMatrix == InMatrix::AnalogClassic
            ? matInverse(rgbToYuvMatrix(kFccKr, kFccKb))
            : matInverse(rgbToYuvMatrix(kBt601Kr, kBt601Kb));
    }
    for (int i = 0; i < 3; i++) {
        double off = 0.0;
        for (int j = 0; j < 3; j++) {
            pipe.pre.m[i][j] = static_cast<float>(decode[i][j] / inNorm.scale[j]);
            off -= decode[i][j] * inNorm.offset[j] / inNorm.scale[j];
        }
        pipe.pre.o[i] = static_cast<float>(off);
    }

    // --- To linear ---
    switch (inTransfer) {
    case InTransfer::Linear:
        pipe.toLinear.kind = TransferOp::Kind::Identity;
        break;
    case InTransfer::Gamma22:
        pipe.toLinear.kind = TransferOp::Kind::PowToLinear;
        pipe.toLinear.p0 = 2.2f;
        break;
    case InTransfer::Gamma28:
        pipe.toLinear.kind = TransferOp::Kind::PowToLinear;
        pipe.toLinear.p0 = 2.8f;
        break;
    case InTransfer::St170Scene:
    case InTransfer::St170Display:
        // ST 170's reproducer EOTF (§5.2) restates the inverse of its camera
        // OETF (§5.1) with an independently rounded signal-domain breakpoint,
        // yet still inverts it exactly: the 4-digit OETF constants make the
        // OETF discontinuous at L=0.018 (linear branch tops out at V=0.0810,
        // power branch starts at V≈0.0813), so no encodable signal lands in
        // (0.0810, 0.0813), and §5.2's 0.0812 threshold sits inside that dead
        // band. Both readings therefore linearize identically.
        pipe.toLinear.kind = TransferOp::Kind::St170ToLinear;
        break;
    case InTransfer::Bt1886Annex1: {
        const double lw = d->contrastIn.value_or(1.0);
        const double lb = d->brightnessIn.value_or(0.0);
        double a, b;
        bt1886AnnexParams(lw, lb, a, b);
        pipe.toLinear.kind = TransferOp::Kind::Bt1886ToLinear;
        pipe.toLinear.p0 = static_cast<float>(a);
        pipe.toLinear.p1 = static_cast<float>(b);
        break;
    }
    case InTransfer::Bt1886Appendix1: {
        const double lw = d->contrastIn.value_or(1.0);
        const double b = d->brightnessIn.value_or(0.0);
        const double k = lw / std::pow(1.0 + b, 2.6);
        pipe.toLinear.kind = TransferOp::Kind::Bt1886CrtToLinear;
        pipe.toLinear.p0 = static_cast<float>(k);
        pipe.toLinear.p1 = static_cast<float>(b);
        pipe.toLinear.p2 = static_cast<float>(k * std::pow(kBt1886Vc + b, 2.6 - 3.0));
        break;
    }
    case InTransfer::Srgb:
        pipe.toLinear.kind = TransferOp::Kind::SrgbToLinear;
        break;
    }

    // --- Gamut: linear RGB in -> XYZ -> (Bradford CAT) -> linear RGB/XYZ out ---
    const Chromaticity inChroma = inChromaticity(inPrim);
    const Chromaticity outChroma = outChromaticity(d->primariesOut);
    Mat3 gamut = rgbToXyzMatrix(inChroma);
    if (d->chromaticAdaptation)
        gamut = matMul(bradfordAdaptationMatrix(inChroma.w, outChroma.w), gamut);
    if (d->primariesOut != OutPrimaries::Xyz)
        gamut = matMul(matInverse(rgbToXyzMatrix(outChroma)), gamut);
    for (int i = 0; i < 3; i++) {
        for (int j = 0; j < 3; j++)
            pipe.gamut.m[i][j] = static_cast<float>(gamut[i][j]);
        pipe.gamut.o[i] = 0.0f;
    }

    // --- From linear ---
    // Built before the encode matrix: a constant-luminance encode applies this
    // curve itself and derives its normalizers from it.
    switch (d->transferOut) {
    case OutTransfer::Linear:
        pipe.toGamma.kind = TransferOp::Kind::Identity;
        break;
    case OutTransfer::Bt1886Annex1: {
        double a, b;
        bt1886AnnexParams(d->contrastOut, d->brightnessOut, a, b);
        pipe.toGamma.kind = TransferOp::Kind::Bt1886FromLinear;
        pipe.toGamma.p0 = static_cast<float>(1.0 / a);
        pipe.toGamma.p1 = static_cast<float>(b);
        break;
    }
    case OutTransfer::Bt2020Oetf:
        pipe.toGamma.kind = TransferOp::Kind::Bt2020OetfFromLinear;
        break;
    case OutTransfer::Srgb:
        pipe.toGamma.kind = TransferOp::Kind::SrgbFromLinear;
        break;
    case OutTransfer::Pq:
        pipe.toGamma.kind = TransferOp::Kind::PqFromLinear;
        pipe.toGamma.p0 = static_cast<float>(d->nominalLuminance / 10000.0);
        break;
    case OutTransfer::Hlg:
        pipe.toGamma.kind = TransferOp::Kind::HlgFromLinear;
        pipe.toGamma.p0 = static_cast<float>(d->nominalLuminance / 1000.0);
        break;
    }

    // --- Encode matrix ---
    Mat3 encode = kIdentity;
    switch (d->matrixOut) {
    case OutMatrix::Rgb:
        break;
    case OutMatrix::Bt709:
        encode = rgbToYuvMatrix(kBt709Kr, kBt709Kb);
        break;
    case OutMatrix::Bt2020Ncl:
        encode = rgbToYuvMatrix(kBt2020Kr, kBt2020Kb);
        break;
    case OutMatrix::Bt2020Cl:
        pipe.useCl = true;
        pipe.cl = makeClParams(pipe.toGamma, kBt2020Kr, kBt2020Kb);
        break;
    case OutMatrix::ChromaticityDerivedCl: {
        double kr, kb;
        lumaWeightsFromPrimaries(outChroma, &kr, &kb);
        pipe.useCl = true;
        pipe.cl = makeClParams(pipe.toGamma, kr, kb);
        break;
    }
    }

    // --- Post: encode matrix + denormalize ---
    const bool outLimited = d->vi.format.colorFamily == cfYUV;
    const PlaneScaling outNorm = normalization(d->vi.format, outLimited);
    for (int i = 0; i < 3; i++) {
        for (int j = 0; j < 3; j++)
            pipe.post.m[i][j] = static_cast<float>(encode[i][j] * outNorm.scale[i]);
        pipe.post.o[i] = static_cast<float>(outNorm.offset[i]);
    }

    return true;
}

// =====================
// Row I/O
// =====================

template <typename T>
void loadRowT(const uint8_t *src, float *dst, int w) {
    const T *s = reinterpret_cast<const T *>(src);
    for (int i = 0; i < w; i++)
        dst[i] = static_cast<float>(s[i]);
}

void loadRow(const VSVideoFormat &f, const uint8_t *src, float *dst, int w) {
    if (f.sampleType == stFloat)
        loadRowT<float>(src, dst, w);
    else if (f.bytesPerSample == 1)
        loadRowT<uint8_t>(src, dst, w);
    else
        loadRowT<uint16_t>(src, dst, w);
}

template <typename T>
void storeRowInt(const float *src, uint8_t *dst, int w, float maxVal) {
    T *d = reinterpret_cast<T *>(dst);
    for (int i = 0; i < w; i++) {
        const float v = std::clamp(std::round(src[i]), 0.0f, maxVal);
        d[i] = static_cast<T>(v);
    }
}

void storeRow(const VSVideoFormat &f, const float *src, uint8_t *dst, int w) {
    if (f.sampleType == stFloat) {
        std::copy_n(src, w, reinterpret_cast<float *>(dst));
    } else {
        const float maxVal = static_cast<float>((1 << f.bitsPerSample) - 1);
        if (f.bytesPerSample == 1)
            storeRowInt<uint8_t>(src, dst, w, maxVal);
        else
            storeRowInt<uint16_t>(src, dst, w, maxVal);
    }
}

// =====================
// VapourSynth callbacks
// =====================

const VSFrame *VS_CC ModernizeGetFrame(int n, int activationReason, void *instanceData,
                                       void **, VSFrameContext *frameCtx, VSCore *core,
                                       const VSAPI *vsapi) {
    auto *d = static_cast<ModernizeData *>(instanceData);

    if (activationReason == arInitial) {
        vsapi->requestFrameFilter(n, d->node, frameCtx);
        return nullptr;
    }
    if (activationReason != arAllFramesReady)
        return nullptr;

    const VSFrame *src = vsapi->getFrameFilter(n, d->node, frameCtx);

    Pipeline pipe;
    std::string err;
    if (!buildPipeline(d, vsapi->getFramePropertiesRO(src), pipe, err, vsapi)) {
        vsapi->setFilterError(("modernize_chromaticity: " + err).c_str(), frameCtx);
        vsapi->freeFrame(src);
        return nullptr;
    }

    const int w = d->vi.width;
    const int h = d->vi.height;
    VSFrame *dst = vsapi->newVideoFrame(&d->vi.format, w, h, src, core);
    if (!dst) {
        vsapi->setFilterError("modernize_chromaticity: failed to allocate output frame",
                              frameCtx);
        vsapi->freeFrame(src);
        return nullptr;
    }

    const uint8_t *sp[3];
    uint8_t *dp[3];
    ptrdiff_t ss[3], ds[3];
    for (int p = 0; p < 3; p++) {
        sp[p] = vsapi->getReadPtr(src, p);
        ss[p] = vsapi->getStride(src, p);
        dp[p] = vsapi->getWritePtr(dst, p);
        ds[p] = vsapi->getStride(dst, p);
    }

    std::vector<float> buf(3 * static_cast<size_t>(w));
    float *r = buf.data();
    float *g = r + w;
    float *b = g + w;

    for (int row = 0; row < h; row++) {
        loadRow(d->inVi.format, sp[0] + row * ss[0], r, w);
        loadRow(d->inVi.format, sp[1] + row * ss[1], g, w);
        loadRow(d->inVi.format, sp[2] + row * ss[2], b, w);

        kAffineRows(pipe.pre, r, g, b, w);
        applyTransfer(pipe.toLinear, r, w);
        applyTransfer(pipe.toLinear, g, w);
        applyTransfer(pipe.toLinear, b, w);
        kAffineRows(pipe.gamut, r, g, b, w);
        if (pipe.useCl) {
            applyConstantLuminance(pipe.cl, pipe.toGamma, r, g, b, w);
        } else {
            applyTransfer(pipe.toGamma, r, w);
            applyTransfer(pipe.toGamma, g, w);
            applyTransfer(pipe.toGamma, b, w);
        }
        kAffineRows(pipe.post, r, g, b, w);

        storeRow(d->vi.format, r, dp[0] + row * ds[0], w);
        storeRow(d->vi.format, g, dp[1] + row * ds[1], w);
        storeRow(d->vi.format, b, dp[2] + row * ds[2], w);
    }

    VSMap *props = vsapi->getFramePropertiesRW(dst);
    vsapi->mapSetInt(props, "_Matrix", d->outMatrixTag, maReplace);
    vsapi->mapSetInt(props, "_Transfer", d->outTransferTag, maReplace);
    vsapi->mapSetInt(props, "_Primaries", d->outPrimariesTag, maReplace);
    const int limited = d->vi.format.colorFamily == cfYUV ? 1 : 0;
    vsapi->mapSetInt(props, "_ColorRange", limited, maReplace);
    vsapi->mapSetInt(props, "_Range", 1 - limited, maReplace);
    // The color math runs at 4:4:4, so chroma siting is moot — unless a
    // trailing downsample will re-site against the original _ChromaLocation.
    if (!d->preserveChromaLocation)
        vsapi->mapDeleteKey(props, "_ChromaLocation");

    vsapi->freeFrame(src);
    return dst;
}

void VS_CC ModernizeFree(void *instanceData, VSCore *, const VSAPI *vsapi) {
    auto *d = static_cast<ModernizeData *>(instanceData);
    vsapi->freeNode(d->node);
    delete d;
}

std::optional<std::string> getOptStringLower(const VSMap *in, const char *key,
                                             const VSAPI *vsapi) {
    int err;
    const char *v = vsapi->mapGetData(in, key, 0, &err);
    if (err || !v)
        return std::nullopt;
    return toLower(v);
}

std::optional<double> getOptFloat(const VSMap *in, const char *key, const VSAPI *vsapi) {
    int err;
    const double v = vsapi->mapGetFloat(in, key, 0, &err);
    if (err)
        return std::nullopt;
    return v;
}

template <typename T, size_t N>
std::optional<T> parseOption(const VSMap *in, const char *key,
                             const Alias<T> (&table)[N], const VSAPI *vsapi,
                             std::string &err) {
    const auto raw = getOptStringLower(in, key, vsapi);
    if (!raw)
        return std::nullopt;
    const auto parsed = parseAlias(table, *raw);
    if (!parsed)
        err = std::string(key) + " value '" + *raw + "' not recognized; expected one of: " +
              aliasNames(table);
    return parsed;
}

} // namespace

void VS_CC CreateModernizeChromaticity(const VSMap *In, VSMap *Out, void *,
                                       VSCore *Core, const VSAPI *vsapi) {
    auto fail = [&](const std::string &msg) {
        vsapi->mapSetError(Out, ("modernize_chromaticity: " + msg).c_str());
    };

    VSNode *node = vsapi->mapGetNode(In, "clip", 0, nullptr);
    auto d = std::make_unique<ModernizeData>();
    d->node = node;
    d->inVi = *vsapi->getVideoInfo(node);

    auto failFree = [&](const std::string &msg) {
        vsapi->freeNode(node);
        fail(msg);
    };

    // --- Input format constraints ---
    if (d->inVi.format.colorFamily == cfUndefined || d->inVi.width == 0) {
        failFree("clips with variable format or dimensions are not supported");
        return;
    }
    if (d->inVi.format.colorFamily == cfGray) {
        failFree("GRAY input carries no chromaticity; supply a YUV 4:4:4 or RGB clip");
        return;
    }
    if (d->inVi.format.sampleType == stInteger && d->inVi.format.bitsPerSample > 16) {
        failFree("integer formats above 16 bits are not supported");
        return;
    }
    if (d->inVi.format.sampleType == stFloat && d->inVi.format.bitsPerSample != 32) {
        failFree("only 32-bit float formats are supported");
        return;
    }

    std::string parseErr;

    // --- Input-side overrides ---
    d->primariesIn = parseOption(In, "primaries_in_s", kInPrimariesAliases, vsapi, parseErr);
    if (!parseErr.empty()) { failFree(parseErr); return; }
    d->transferIn = parseOption(In, "transfer_in_s", kInTransferAliases, vsapi, parseErr);
    if (!parseErr.empty()) { failFree(parseErr); return; }
    d->matrixIn = parseOption(In, "matrix_in_s", kInMatrixAliases, vsapi, parseErr);
    if (!parseErr.empty()) { failFree(parseErr); return; }

    if (d->matrixIn && d->inVi.format.colorFamily == cfRGB) {
        failFree("matrix_in_s does not apply to RGB input");
        return;
    }

    d->contrastIn = getOptFloat(In, "contrast_in", vsapi);
    d->brightnessIn = getOptFloat(In, "brightness_in", vsapi);
    for (const auto &[name, value] : {std::pair{"contrast_in", d->contrastIn},
                                      std::pair{"brightness_in", d->brightnessIn}}) {
        if (value && (*value < 0.0 || *value > 1.0)) {
            failFree(std::string(name) + " must be within 0.0-1.0");
            return;
        }
    }
    if (d->contrastIn && *d->contrastIn == 0.0) {
        failFree("contrast_in must be greater than 0.0");
        return;
    }
    if (d->contrastIn && d->brightnessIn && *d->brightnessIn >= *d->contrastIn) {
        failFree("brightness_in (black lift) must be below contrast_in (white level)");
        return;
    }

    // --- Output side: preset first, explicit parameters override ---
    const auto preset = parseOption(In, "output_preset", kOutputPresetAliases, vsapi, parseErr);
    if (!parseErr.empty()) { failFree(parseErr); return; }
    const auto primariesOut = parseOption(In, "primaries_s", kOutPrimariesAliases, vsapi, parseErr);
    if (!parseErr.empty()) { failFree(parseErr); return; }
    const auto transferOut = parseOption(In, "transfer_s", kOutTransferAliases, vsapi, parseErr);
    if (!parseErr.empty()) { failFree(parseErr); return; }
    const auto matrixOut = parseOption(In, "matrix_s", kOutMatrixAliases, vsapi, parseErr);
    if (!parseErr.empty()) { failFree(parseErr); return; }

    std::optional<OutPrimaries> resolvedPrimaries = primariesOut;
    std::optional<OutTransfer> resolvedTransfer = transferOut;
    std::optional<OutMatrix> resolvedMatrix = matrixOut;
    if (preset) {
        switch (*preset) {
        case OutputPreset::Hdtv:
            if (!resolvedPrimaries) resolvedPrimaries = OutPrimaries::Bt709;
            if (!resolvedTransfer) resolvedTransfer = OutTransfer::Bt1886Annex1;
            if (!resolvedMatrix) resolvedMatrix = OutMatrix::Bt709;
            break;
        case OutputPreset::Bt2100Pq:
            if (!resolvedPrimaries) resolvedPrimaries = OutPrimaries::Bt2020;
            if (!resolvedTransfer) resolvedTransfer = OutTransfer::Pq;
            if (!resolvedMatrix) resolvedMatrix = OutMatrix::Bt2020Ncl;
            break;
        case OutputPreset::Bt2100Hlg:
            if (!resolvedPrimaries) resolvedPrimaries = OutPrimaries::Bt2020;
            if (!resolvedTransfer) resolvedTransfer = OutTransfer::Hlg;
            if (!resolvedMatrix) resolvedMatrix = OutMatrix::Bt2020Ncl;
            break;
        case OutputPreset::Bt2020Sdr:
            if (!resolvedPrimaries) resolvedPrimaries = OutPrimaries::Bt2020;
            if (!resolvedTransfer) resolvedTransfer = OutTransfer::Bt1886Annex1;
            if (!resolvedMatrix) resolvedMatrix = OutMatrix::Bt2020Ncl;
            break;
        // IEC 61966-2-1 defines no color-difference form, so this is the one
        // preset that lands on RGB. matrix_s still overrides.
        case OutputPreset::Srgb:
            if (!resolvedPrimaries) resolvedPrimaries = OutPrimaries::Bt709;
            if (!resolvedTransfer) resolvedTransfer = OutTransfer::Srgb;
            if (!resolvedMatrix) resolvedMatrix = OutMatrix::Rgb;
            break;
        }
    }

    // Constant luminance changes where the transfer curve is applied, not
    // which one — H.273 E-62 to E-65 derive the color-difference normalizers
    // from whichever transfer is signalled — so transfer_s stays free here.
    // Naming none falls back to the BT.2020 OETF, the pairing BT.2020's own
    // Table 4 tabulates.
    const bool constantLuminance =
        resolvedMatrix == OutMatrix::Bt2020Cl ||
        resolvedMatrix == OutMatrix::ChromaticityDerivedCl;
    if (constantLuminance) {
        // Matrix 10 means BT.2020's luma weights by definition; matrix 13
        // derives them from the output primaries instead.
        if (resolvedMatrix == OutMatrix::Bt2020Cl) {
            if (resolvedPrimaries && resolvedPrimaries != OutPrimaries::Bt2020) {
                failFree("matrix_s '2020cl' requires bt2020 primaries; use "
                         "'chromacl' for constant luminance against other primaries");
                return;
            }
            resolvedPrimaries = OutPrimaries::Bt2020;
        }
        if (!resolvedTransfer)
            resolvedTransfer = OutTransfer::Bt2020Oetf;
    }

    if (!resolvedPrimaries) {
        failFree("output primaries required: set primaries_s or output_preset");
        return;
    }
    if (!resolvedTransfer) {
        failFree("output transfer required: set transfer_s or output_preset");
        return;
    }
    if (!resolvedMatrix) {
        if (d->inVi.format.colorFamily == cfRGB) {
            resolvedMatrix = OutMatrix::Rgb;  // RGB in, RGB out by default
        } else {
            failFree("output matrix required: set matrix_s (or 'rgb') or output_preset");
            return;
        }
    }

    if (resolvedPrimaries == OutPrimaries::Xyz && resolvedMatrix != OutMatrix::Rgb) {
        failFree("primaries_s 'xyz' requires matrix_s 'rgb'");
        return;
    }

    d->primariesOut = *resolvedPrimaries;
    d->transferOut = *resolvedTransfer;
    d->matrixOut = *resolvedMatrix;

    // --- Output tuning ---
    const auto contrastOpt = getOptFloat(In, "contrast", vsapi);
    const auto brightnessOpt = getOptFloat(In, "brightness", vsapi);
    d->contrastOut = contrastOpt.value_or(1.0);
    d->brightnessOut = brightnessOpt.value_or(0.0);
    if ((contrastOpt || brightnessOpt) &&
        d->transferOut != OutTransfer::Bt1886Annex1) {
        failFree("contrast/brightness only apply to the bt1886-annex-1 output transfer");
        return;
    }
    if (d->contrastOut <= 0.0 || d->contrastOut > 1.0) {
        failFree("contrast must be within 0.0-1.0 and greater than 0.0");
        return;
    }
    if (d->brightnessOut < 0.0 || d->brightnessOut >= d->contrastOut) {
        failFree("brightness (black lift) must be within 0.0-1.0 and below contrast");
        return;
    }

    d->nominalLuminance = getOptFloat(In, "nominal_luminance", vsapi).value_or(100.0);
    if (d->nominalLuminance <= 0.0) {
        failFree("nominal_luminance must be positive");
        return;
    }

    int errFlag;
    const int64_t cat = vsapi->mapGetInt(In, "chromatic_adaptation", 0, &errFlag);
    d->chromaticAdaptation = !errFlag && cat != 0;

    const VSColorFamily outFamily = d->matrixOut == OutMatrix::Rgb ? cfRGB : cfYUV;

    // --- Subsampled chroma: delegate the resampling to the resize plugin ---
    // The color math itself always runs at 4:4:4; subsampled Y'CbCr input is
    // upsampled through resize.<resample_filter_uv> (which sites against the
    // frame's _ChromaLocation itself) and, for Y'CbCr output, brought back to
    // the input subsampling with the same kernel afterwards — the same round
    // trip the resize functions perform for colorimetry changes.
    resizeuv::Options uv;
    if (!resizeuv::readOptions(In, uv, parseErr, vsapi)) { failFree(parseErr); return; }

    const bool inputSubsampled = d->inVi.format.colorFamily == cfYUV &&
        (d->inVi.format.subSamplingW != 0 || d->inVi.format.subSamplingH != 0);
    VSPlugin *resizePlugin = nullptr;
    uint32_t origFormatId = 0;
    if (inputSubsampled) {
        if (d->inVi.format.subSamplingW == 0) {
            failFree("4:4:0 chroma is not resampled automatically: if this is "
                     "SECAM from decode_4fsc_video, the chroma planes are a "
                     "line-sequential Db/Dr lattice that plain resampling would "
                     "blend; realign with resample_secam, or fill_secam_by_delay "
                     "for the classic delay-line treatment");
            return;
        }
        resizePlugin = vsapi->getPluginByID(VSH_RESIZE_PLUGIN_ID, Core);
        if (!resizePlugin) {
            failFree("resize plugin not available for chroma resampling");
            return;
        }
        origFormatId = vsapi->queryVideoFormatID(
            cfYUV, d->inVi.format.sampleType, d->inVi.format.bitsPerSample,
            d->inVi.format.subSamplingW, d->inVi.format.subSamplingH, Core);
        const uint32_t id444 = vsapi->queryVideoFormatID(
            cfYUV, d->inVi.format.sampleType, d->inVi.format.bitsPerSample, 0, 0, Core);
        std::string invokeErr;
        VSNode *upNode = resizeuv::toFormat(resizePlugin, node, id444, uv, std::nullopt,
                                            std::nullopt, invokeErr, vsapi);
        if (!upNode) {  // node was consumed by the invoke
            fail("chroma upsample failed: " + invokeErr);
            return;
        }
        node = upNode;
        d->node = node;
        d->inVi = *vsapi->getVideoInfo(node);
        d->preserveChromaLocation = outFamily == cfYUV;
    }

    // --- Output video format ---
    d->vi = d->inVi;
    if (!vsapi->queryVideoFormat(&d->vi.format, outFamily, d->inVi.format.sampleType,
                                 d->inVi.format.bitsPerSample, 0, 0, Core)) {
        failFree("failed to query output video format");
        return;
    }

    // --- H.273 frame property tags ---
    switch (d->matrixOut) {
    case OutMatrix::Rgb: d->outMatrixTag = 0; break;
    case OutMatrix::Bt709: d->outMatrixTag = 1; break;
    case OutMatrix::Bt2020Ncl: d->outMatrixTag = 9; break;
    case OutMatrix::Bt2020Cl: d->outMatrixTag = 10; break;
    case OutMatrix::ChromaticityDerivedCl: d->outMatrixTag = 13; break;
    }
    switch (d->primariesOut) {
    case OutPrimaries::Bt709: d->outPrimariesTag = 1; break;
    case OutPrimaries::Bt2020: d->outPrimariesTag = 9; break;
    case OutPrimaries::Xyz: d->outPrimariesTag = 10; break;
    case OutPrimaries::P3Dci: d->outPrimariesTag = 11; break;
    case OutPrimaries::P3D65: d->outPrimariesTag = 12; break;
    }
    // BT.2020 OETF tags come in 10- and 12-bit flavours; pick by output depth,
    // treating float as the wide one.
    const bool deepOutput = d->vi.format.sampleType == stFloat ||
                            d->vi.format.bitsPerSample >= 12;
    switch (d->transferOut) {
    case OutTransfer::Linear: d->outTransferTag = 8; break;
    case OutTransfer::Bt1886Annex1:
    case OutTransfer::Bt2020Oetf:
        // Both are signalled through the BT.709/BT.2020 OETF system tag —
        // BT.709 normally, BT.2020's own with 2020 primaries. For BT.1886
        // that leans on H.273's note that those code points, though defined
        // as an OETF, take BT.1886 as their corresponding reference EOTF.
        d->outTransferTag = d->primariesOut == OutPrimaries::Bt2020
            ? (deepOutput ? 15 : 14) : 1;
        break;
    case OutTransfer::Srgb: d->outTransferTag = 13; break;
    case OutTransfer::Pq: d->outTransferTag = 16; break;
    case OutTransfer::Hlg: d->outTransferTag = 18; break;
    }

    VSFilterDependency deps[] = {{node, rpStrictSpatial}};
    vsapi->createVideoFilter(Out, "modernize_chromaticity", &d->vi, ModernizeGetFrame,
                             ModernizeFree, fmParallel, deps, 1, d.get(), Core);
    d.release();

    // Y'CbCr output from subsampled input goes back to the input subsampling.
    if (inputSubsampled && outFamily == cfYUV) {
        VSNode *converted = vsapi->mapGetNode(Out, "clip", 0, &errFlag);
        if (errFlag)
            return;  // createVideoFilter failed and set the error itself
        vsapi->clearMap(Out);
        std::string invokeErr;
        VSNode *downNode = resizeuv::toFormat(resizePlugin, converted, origFormatId, uv,
                                              std::nullopt, std::nullopt, invokeErr, vsapi);
        if (!downNode) {
            fail("chroma downsample failed: " + invokeErr);
            return;
        }
        vsapi->mapConsumeNode(Out, "clip", downNode, maReplace);
    }
}
