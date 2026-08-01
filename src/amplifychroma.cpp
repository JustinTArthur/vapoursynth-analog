/******************************************************************************
 * amplifychroma.cpp
 * vapoursynth-analog - analog-domain chroma gain
 *
 * Scales the color-difference signals the way a composite decoder's
 * chroma_gain does, but after the decode: saturation as the analog video
 * domain defines it rather than an HSV/HLS one. Frames that don't already
 * carry E'Y E'Cb E'Cr are converted through a Matrix 6 intermediate for the
 * operation and converted back.
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 ******************************************************************************/

#include "amplifychroma.h"

#include "resizeuv.h"

#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include <VapourSynth4.h>
#include <VSHelper4.h>

namespace {

// E'Cb/E'Cr as the analog standards define them, so a frame tagged with one of
// these already carries the color differences a decoder's chroma gain would
// have scaled. All three are the same split — the luma coefficients NTSC-1953
// derived from its primaries at Illuminant C — with code 4 at the precision it
// was first published to and codes 5 and 6 at the one later systems restated
// it to, keeping the coefficients even as their primaries and white moved.
bool isAnalogColorDifference(int64_t matrix) {
    return matrix == 4 || matrix == 5 || matrix == 6;
}

// Analog-era colorimetry, as H.273 codes it: NTSC-1953 (4), EBU (5), SMPTE
// ST 170 (6) and SMPTE ST 240 (7). Absence and "unspecified" pass too — plenty
// of captures carry no tag at all, and analog material is the assumption here.
bool isAnalogPrimaries(int64_t primaries) {
    return primaries == 2 || (primaries >= 4 && primaries <= 7);
}

// False with err set when the frame's colorimetry is from outside the era this
// operation belongs to. Checked on both paths: no signal was ever built by
// splitting luma from chroma with the analog coefficients on newer primaries,
// so scaling those color differences describes nothing.
bool checkAnalogPrimaries(const VSMap *props, std::string &err, const VSAPI *vsapi) {
    int mapErr;
    const int64_t primaries = vsapi->mapGetInt(props, "_Primaries", 0, &mapErr);
    if (mapErr || isAnalogPrimaries(primaries))
        return true;
    err = "_Primaries frame property value " + std::to_string(primaries) +
          " is not analog-era colorimetry, and no era outside it built its color "
          "differences this way; there is also no telling what the picture was "
          "originally broadcast in. Amplify before modernize_chromaticity (or "
          "another conversion) rather than after. Accepted: 4 (NTSC-1953), "
          "5 (EBU), 6 (SMPTE ST 170), 7 (SMPTE ST 240), or 2/absent for unknown";
    return false;
}

// Non-analog H.273 matrix code points the resize plugin can convert between.
// One round trip is built per value, since the conversion back to the source
// format has to name its output matrix while the frames pick it. The analog
// ones are absent on purpose: they need no matrix change at all.
constexpr int kConvertibleMatrices[] = {1, 7, 8, 9, 10, 12, 13, 14};
constexpr int kMatrixSlots = 15;

const char *const k440Advice =
    "4:4:0 chroma is not resampled automatically: if this is SECAM from "
    "decode_4fsc_video, the chroma planes are a line-sequential Db/Dr lattice "
    "that plain resampling would blend; realign with resample_secam, or "
    "fill_secam_by_delay for the classic delay-line treatment";

// =====================
// The gain itself
// =====================

// Float Y'CbCr centres chroma on 0.0 whatever the range, so the gain is a
// plain scale of the two color-difference planes.
VSFrame *scaleChroma(const VSVideoInfo &vi, float gain, const VSFrame *src, VSCore *core,
                     const VSAPI *vsapi) {
    // Luma is untouched, so the output frame just points at the source plane.
    const VSFrame *planeSrc[3] = {src, nullptr, nullptr};
    constexpr int planes[3] = {0, 0, 0};
    VSFrame *dst = vsapi->newVideoFrame2(&vi.format, vi.width, vi.height, planeSrc,
                                         planes, src, core);
    if (!dst)
        return nullptr;

    for (int p = 1; p < 3; p++) {
        const int w = vsapi->getFrameWidth(dst, p);
        const int h = vsapi->getFrameHeight(dst, p);
        constexpr auto sampleSize = static_cast<ptrdiff_t>(sizeof(float));
        const ptrdiff_t srcStride = vsapi->getStride(src, p) / sampleSize;
        const ptrdiff_t dstStride = vsapi->getStride(dst, p) / sampleSize;
        const float *s = reinterpret_cast<const float *>(vsapi->getReadPtr(src, p));
        float *d = reinterpret_cast<float *>(vsapi->getWritePtr(dst, p));
        for (int y = 0; y < h; y++, s += srcStride, d += dstStride) {
            for (int x = 0; x < w; x++)
                d[x] = s[x] * gain;
        }
    }
    return dst;
}

// =====================
// Unconditional gain filter
// =====================

// Sits at the end of a conversion round trip, where the clip is known to be
// float Y'CbCr on the Matrix 6 axes.
struct GainData {
    VSNode *node = nullptr;
    VSVideoInfo vi = {};
    float gain = 1.0f;
};

const VSFrame *VS_CC GainGetFrame(int n, int activationReason, void *instanceData, void **,
                                  VSFrameContext *frameCtx, VSCore *core,
                                  const VSAPI *vsapi) {
    auto *d = static_cast<GainData *>(instanceData);

    if (activationReason == arInitial) {
        vsapi->requestFrameFilter(n, d->node, frameCtx);
        return nullptr;
    }
    if (activationReason != arAllFramesReady)
        return nullptr;

    const VSFrame *src = vsapi->getFrameFilter(n, d->node, frameCtx);
    // Reached on the RGB round trip, whose frames pass no other check; the
    // Y'CbCr ones were already vetted before they were routed here.
    std::string err;
    if (!checkAnalogPrimaries(vsapi->getFramePropertiesRO(src), err, vsapi)) {
        vsapi->setFilterError(("amplify_chroma: " + err).c_str(), frameCtx);
        vsapi->freeFrame(src);
        return nullptr;
    }

    VSFrame *dst = scaleChroma(d->vi, d->gain, src, core, vsapi);
    vsapi->freeFrame(src);
    if (!dst)
        vsapi->setFilterError("amplify_chroma: failed to allocate output frame", frameCtx);
    return dst;
}

void VS_CC GainFree(void *instanceData, VSCore *, const VSAPI *vsapi) {
    auto *d = static_cast<GainData *>(instanceData);
    vsapi->freeNode(d->node);
    delete d;
}

// =====================
// Per-frame path selection
// =====================

// Applies the gain in place on frames that already carry analog color
// differences, and hands the rest to the round trip built for their matrix.
struct SelectData {
    VSNode *node = nullptr;
    VSVideoInfo vi = {};
    float gain = 1.0f;
    bool directEligible = false;              // Format allows the in-place gain
    // Depth-only float round trip for analog color differences that aren't
    // already float. Names no matrix, so resize converts the samples and
    // nothing else — no color-difference resampling, whichever of the three
    // analog matrices the frame carries.
    VSNode *depthOnly = nullptr;
    // Round trips through the Matrix 6 axes, indexed by source _Matrix. Absent
    // for the 4:4:0 lattice, whose chroma a matrix change would have to
    // resample.
    VSNode *matrixed[kMatrixSlots] = {};
    bool lattice440 = false;  // For the error a frame needing a matrix gets
};

const VSFrame *VS_CC SelectGetFrame(int n, int activationReason, void *instanceData,
                                    void **frameData, VSFrameContext *frameCtx,
                                    VSCore *core, const VSAPI *vsapi) {
    auto *d = static_cast<SelectData *>(instanceData);

    if (activationReason == arInitial) {
        vsapi->requestFrameFilter(n, d->node, frameCtx);
        return nullptr;
    }
    if (activationReason != arAllFramesReady)
        return nullptr;

    // Second pass: the round trip this frame was routed to has finished.
    if (auto *routed = static_cast<VSNode *>(*frameData))
        return vsapi->getFrameFilter(n, routed, frameCtx);

    const VSFrame *src = vsapi->getFrameFilter(n, d->node, frameCtx);
    const VSMap *props = vsapi->getFramePropertiesRO(src);

    std::string primariesErr;
    if (!checkAnalogPrimaries(props, primariesErr, vsapi)) {
        vsapi->setFilterError(("amplify_chroma: " + primariesErr).c_str(), frameCtx);
        vsapi->freeFrame(src);
        return nullptr;
    }

    int err;
    const int64_t matrix = vsapi->mapGetInt(props, "_Matrix", 0, &err);
    const bool analog = !err && isAnalogColorDifference(matrix);

    if (analog && d->directEligible) {
        VSFrame *dst = scaleChroma(d->vi, d->gain, src, core, vsapi);
        vsapi->freeFrame(src);
        if (!dst)
            vsapi->setFilterError("amplify_chroma: failed to allocate output frame",
                                  frameCtx);
        return dst;
    }
    vsapi->freeFrame(src);

    // Already the right color differences, just not in a format the gain can
    // be applied to; otherwise they have to be moved onto the analog axes.
    VSNode *routed = analog ? d->depthOnly
                            : (matrix >= 0 && matrix < kMatrixSlots ? d->matrixed[matrix]
                                                                    : nullptr);
    if (!routed) {
        std::string errMsg;
        if (!analog && (err || matrix == 2)) {
            errMsg = "input matrix unknown: no usable _Matrix frame property, so the "
                     "color differences to amplify can't be identified; tag the clip "
                     "(std.SetFrameProps(_Matrix=6) for an analog decode) or convert "
                     "it with resize first";
        } else if (!analog && d->lattice440) {
            errMsg = "_Matrix frame property value " + std::to_string(matrix) +
                     " puts the color differences on other axes, and moving them "
                     "onto the analog ones would resample this clip's chroma: " +
                     k440Advice;
        } else if (!analog) {
            errMsg = "the resize plugin offers no conversion for _Matrix frame "
                     "property value " + std::to_string(matrix);
        } else {
            errMsg = "the resize plugin could not convert this format for the gain";
        }
        vsapi->setFilterError(("amplify_chroma: " + errMsg).c_str(), frameCtx);
        return nullptr;
    }

    *frameData = routed;
    vsapi->requestFrameFilter(n, routed, frameCtx);
    return nullptr;
}

void VS_CC SelectFree(void *instanceData, VSCore *, const VSAPI *vsapi) {
    auto *d = static_cast<SelectData *>(instanceData);
    vsapi->freeNode(d->node);
    vsapi->freeNode(d->depthOnly);
    for (VSNode *node : d->matrixed)
        vsapi->freeNode(node);
    delete d;
}

// =====================
// Construction
// =====================

// clip -> YUV 4:x:x 32-bit float -> chroma gain, keeping the source
// subsampling. matrixOut names the axes to land on, or is left unset to
// convert the samples and nothing else — resize resamples chroma only for a
// color-difference change, so an unset matrix keeps even the 4:4:0 lattice
// intact. The input matrix always comes from the frame properties, so one
// chain serves every source matrix; only a conversion back has to name one.
// Consumes the clip reference; returns nullptr with err set on failure.
VSNode *buildGainChain(VSPlugin *resizePlugin, VSNode *clip, const VSVideoInfo &srcVi,
                       const resizeuv::Options &uv, std::optional<int> matrixOut,
                       float gain, VSCore *core, std::string &err, const VSAPI *vsapi) {
    const bool yuv = srcVi.format.colorFamily == cfYUV;
    const uint32_t intermediateId = vsapi->queryVideoFormatID(
        cfYUV, stFloat, 32, yuv ? srcVi.format.subSamplingW : 0,
        yuv ? srcVi.format.subSamplingH : 0, core);
    VSNode *converted = resizeuv::toFormat(resizePlugin, clip, intermediateId, uv,
                                           std::nullopt, matrixOut, err, vsapi);
    if (!converted)
        return nullptr;

    auto d = std::make_unique<GainData>();
    d->node = converted;
    d->vi = *vsapi->getVideoInfo(converted);
    d->gain = gain;
    VSFilterDependency deps[] = {{converted, rpStrictSpatial}};
    VSNode *gained =
        vsapi->createVideoFilter2("amplify_chroma", &d->vi, GainGetFrame, GainFree,
                                  fmParallel, deps, 1, d.get(), core);
    if (!gained) {
        err = "failed to create the internal chroma gain filter";
        vsapi->freeNode(converted);
        return nullptr;
    }
    d.release();
    return gained;
}

}  // namespace

void VS_CC CreateAmplifyChroma(const VSMap *In, VSMap *Out, void *, VSCore *Core,
                               const VSAPI *vsapi) {
    auto fail = [&](const std::string &msg) {
        vsapi->mapSetError(Out, ("amplify_chroma: " + msg).c_str());
    };

    VSNode *node = vsapi->mapGetNode(In, "clip", 0, nullptr);
    const VSVideoInfo vi = *vsapi->getVideoInfo(node);

    auto failFree = [&](const std::string &msg) {
        vsapi->freeNode(node);
        fail(msg);
    };

    if (vi.format.colorFamily == cfUndefined || vi.width == 0) {
        failFree("clips with variable format or dimensions are not supported");
        return;
    }
    if (vi.format.colorFamily == cfGray) {
        failFree("GRAY input carries no chroma to amplify");
        return;
    }

    int err;
    const double gain = vsapi->mapGetFloat(In, "gain", 0, &err);
    if (err) {
        failFree("gain is required");
        return;
    }
    if (gain < 0.0) {
        failFree("gain must be 0.0 or greater; 0.0 leaves a monochrome picture");
        return;
    }

    resizeuv::Options uv;
    std::string uvErr;
    if (!resizeuv::readOptions(In, uv, uvErr, vsapi)) {
        failFree(uvErr);
        return;
    }

    // Unity gain would otherwise pay for a conversion round trip that can only
    // cost precision, so hand the clip straight back.
    if (gain == 1.0) {
        vsapi->mapConsumeNode(Out, "clip", node, maReplace);
        return;
    }

    const bool isYuv = vi.format.colorFamily == cfYUV;
    // The in-place gain needs the color differences as decoded: float Y'CbCr,
    // which is what decode_4fsc_video emits.
    const bool directEligible = isYuv && vi.format.sampleType == stFloat;
    const bool lattice440 =
        isYuv && vi.format.subSamplingW == 0 && vi.format.subSamplingH != 0;

    // Float Y'CbCr on the 4:4:0 lattice is the one input that never converts:
    // its analog frames are scaled in place, and its others are refused.
    const bool needsResize = !(isYuv && directEligible && lattice440);
    VSPlugin *resizePlugin = nullptr;
    if (needsResize) {
        resizePlugin = vsapi->getPluginByID(VSH_RESIZE_PLUGIN_ID, Core);
        if (!resizePlugin) {
            failFree("resize plugin not available for the conversions this input needs");
            return;
        }
    }

    const uint32_t srcFormatId = vsapi->queryVideoFormatID(
        vi.format.colorFamily, vi.format.sampleType, vi.format.bitsPerSample,
        vi.format.subSamplingW, vi.format.subSamplingH, Core);

    // RGB has no color differences of its own, and no _Matrix to read: every
    // frame takes the same round trip, so build it as a plain chain.
    if (!isYuv) {
        std::string invokeErr;
        VSNode *gained = buildGainChain(resizePlugin, node, vi, uv, 6,
                                        static_cast<float>(gain), Core, invokeErr, vsapi);
        if (!gained) {  // node was consumed by the invoke
            fail("conversion to a Matrix 6 intermediate failed: " + invokeErr);
            return;
        }
        VSNode *back = resizeuv::toFormat(resizePlugin, gained, srcFormatId, uv, 6,
                                          std::nullopt, invokeErr, vsapi);
        if (!back) {
            fail("conversion back to the source format failed: " + invokeErr);
            return;
        }
        vsapi->mapConsumeNode(Out, "clip", back, maReplace);
        return;
    }

    auto d = std::make_unique<SelectData>();
    d->node = node;
    d->vi = vi;
    d->gain = static_cast<float>(gain);
    d->directEligible = directEligible;
    d->lattice440 = lattice440;

    std::vector<VSFilterDependency> deps;
    deps.push_back({node, rpStrictSpatial});

    std::string invokeErr;

    // Analog color differences that only need a format the gain can be
    // applied to. Naming no matrix keeps it a conversion of the samples alone,
    // which the 4:4:0 lattice survives, so this is built for every subsampling.
    if (!directEligible) {
        VSNode *gained = buildGainChain(resizePlugin, vsapi->addNodeRef(node), vi, uv,
                                        std::nullopt, d->gain, Core, invokeErr, vsapi);
        if (!gained) {
            failFree("conversion to a float intermediate failed: " + invokeErr);
            return;
        }
        VSNode *back = resizeuv::toFormat(resizePlugin, gained, srcFormatId, uv,
                                          std::nullopt, std::nullopt, invokeErr, vsapi);
        if (!back) {
            failFree("conversion back to the source format failed: " + invokeErr);
            return;
        }
        d->depthOnly = back;
        deps.push_back({back, rpStrictSpatial});
    }

    // Color differences on other axes, which have to be moved onto the analog
    // ones. That is a real color-difference change, so resize resamples the
    // chroma of a subsampled clip to make it — which the 4:4:0 lattice would
    // not survive, leaving those frames to be refused instead.
    if (!lattice440) {
        VSNode *gained = buildGainChain(resizePlugin, vsapi->addNodeRef(node), vi, uv, 6,
                                        d->gain, Core, invokeErr, vsapi);
        if (!gained) {
            failFree("conversion to a Matrix 6 intermediate failed: " + invokeErr);
            return;
        }
        for (int matrix : kConvertibleMatrices) {
            VSNode *back = resizeuv::toFormat(resizePlugin, vsapi->addNodeRef(gained),
                                              srcFormatId, uv, 6, matrix, invokeErr, vsapi);
            // A matrix this resize build won't convert is reported at frame
            // time, and only for frames that actually carry it.
            if (!back)
                continue;
            d->matrixed[matrix] = back;
            deps.push_back({back, rpStrictSpatial});
        }
        vsapi->freeNode(gained);
    }

    vsapi->createVideoFilter(Out, "amplify_chroma", &d->vi, SelectGetFrame, SelectFree,
                             fmParallel, deps.data(), static_cast<int>(deps.size()),
                             d.get(), Core);
    d.release();
}
