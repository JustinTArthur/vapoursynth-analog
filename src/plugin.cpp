/******************************************************************************
 * plugin.cpp
 * vapoursynth-analog - VapourSynth plugin entry point
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 ******************************************************************************/

#include "version.h"
#include "analog4fsc.h"

#include <filesystem>
#include <memory>

#include <VapourSynth4.h>
#include <VSHelper4.h>

#include <QCoreApplication>

// Ensure Qt is initialized for SQL support
static std::unique_ptr<QCoreApplication> qtApp;
static int fakeArgc = 1;
static char fakeArgv0[] = "vsanalog";
static char *fakeArgv[] = { fakeArgv0, nullptr };

static void ensureQtInitialized() {
    if (!QCoreApplication::instance()) {
        qtApp = std::make_unique<QCoreApplication>(fakeArgc, fakeArgv);
    }
}

// Decode configuration data passed to filter callbacks
struct DecodeConfig {
    VSVideoInfo VI = {};
    std::unique_ptr<VSAnalog4fscSource> V;
    int64_t FPSNum = -1;
    int64_t FPSDen = -1;
    bool RFF = false;
    bool isMono = false;           // True when using mono decoder (GRAYS output)
    bool isNTSC = false;           // True for NTSC/PAL-M, false for PAL
    int firstActiveFrameLine = 0;  // For field order calculation
    int sarNum = 1;                // Sample aspect ratio numerator
    int sarDen = 1;                // Sample aspect ratio denominator
};

// Frame getter callback
static const VSFrame *VS_CC VSAnalog4fscSourceGetFrame(
    int n, int activationReason, void *instanceData, void **,
    VSFrameContext *frameCtx, VSCore *core, const VSAPI *vsapi
) {
    auto *D = static_cast<DecodeConfig *>(instanceData);

    if (activationReason != arInitial) {
        return nullptr;
    }

    // Create the output frame
    VSFrame *dst = vsapi->newVideoFrame(&D->VI.format, D->VI.width, D->VI.height, nullptr, core);
    if (!dst) {
        vsapi->setFilterError("Failed to allocate output frame", frameCtx);
        return nullptr;
    }

    // Get write pointers and strides for each plane
    auto *yData = reinterpret_cast<float *>(vsapi->getWritePtr(dst, 0));
    ptrdiff_t yStride = vsapi->getStride(dst, 0);

    // For mono output, we only have Y plane; for YUV we have all three
    float *uData = nullptr;
    float *vData = nullptr;
    ptrdiff_t uStride = 0;
    ptrdiff_t vStride = 0;

    if (!D->isMono) {
        uData = reinterpret_cast<float *>(vsapi->getWritePtr(dst, 1));
        vData = reinterpret_cast<float *>(vsapi->getWritePtr(dst, 2));
        uStride = vsapi->getStride(dst, 1);
        vStride = vsapi->getStride(dst, 2);
    }

    // Decode the frame
    try {
        if (!D->V->GetFrame(n, yData, uData, vData,
                           static_cast<int>(yStride),
                           static_cast<int>(uStride),
                           static_cast<int>(vStride))) {
            vsapi->freeFrame(dst);
            vsapi->setFilterError("Failed to decode frame", frameCtx);
            return nullptr;
        }
    } catch (const std::exception &e) {
        vsapi->freeFrame(dst);
        vsapi->setFilterError(e.what(), frameCtx);
        return nullptr;
    }

    // Set frame properties for color metadata
    VSMap *props = vsapi->getFramePropertiesRW(dst);

    // Color primaries and matrix coefficients
    // NTSC (SMPTE 170M): _Primaries=6, _Matrix=6
    // PAL (BT.470BG): _Primaries=5, _Matrix=5
    // Transfer is BT.709/BT.601 for both: _Transfer=1
    if (D->isNTSC) {
        vsapi->mapSetInt(props, "_Primaries", 6, maReplace);
        vsapi->mapSetInt(props, "_Matrix", 6, maReplace);
    } else {
        vsapi->mapSetInt(props, "_Primaries", 5, maReplace);
        vsapi->mapSetInt(props, "_Matrix", 5, maReplace);
    }
    vsapi->mapSetInt(props, "_Transfer", 1, maReplace);

    // Most video pipelines don't have a concept of limited-range
    // floating-point matrix-derived video. This includes the
    // VapourSynth's built-in resize plugin. Samples are
    // effectively at full ranges (0.0-1.0 for luma,
    // -0.5 to 0.5 for color difference channels) that map to the limited
    // ranges in integer value systems. Because the resize plugin (zimg)
    // doesn't distinguish between limited and full float but uses it to determine
    // a within-matrix conversion target range, we'll mark it as limited so
    // that downstream conversions to integer Y′CbCr samples will stay marked
    // as limited without the user needing to specify.
    // AviSynth-style range property:
    vsapi->mapSetInt(props, "_ColorRange", 1, maReplace);
    // ITU H.273 code point as used by resize plugin (zimg):
    vsapi->mapSetInt(props, "_Range", 0, maReplace);

    // Field order - matches ld-chroma-decoder's Y4M output logic
    // Ib (bottom field first) = 1, It (top field first) = 2
    // Logic: if (firstActiveFrameLine % 2) is odd -> BFF, else TFF
    // (We don't have padding, so topPadLines is always 0)
    int fieldBased = (D->firstActiveFrameLine % 2 == 1) ? 1 : 2;
    vsapi->mapSetInt(props, "_FieldBased", fieldBased, maReplace);

    // Sample Aspect Ratio based on sampling and video system
    vsapi->mapSetInt(props, "_SARNum", D->sarNum, maReplace);
    vsapi->mapSetInt(props, "_SARDen", D->sarDen, maReplace);

    return dst;
}

// Cleanup callback
static void VS_CC VSAnalog4fscSourceFree(void *instanceData, VSCore *, const VSAPI *) {
    delete static_cast<DecodeConfig *>(instanceData);
}

// Filter creation function
static void VS_CC Create4fscSource(const VSMap *In, VSMap *Out, void *, VSCore *Core, const VSAPI *vsapi) {
    int err;

    // Ensure Qt is initialized (required for SQL database access)
    ensureQtInitialized();

    // Get the primary source path (composite or luma)
    const char *RawSourcePath = vsapi->mapGetData(In, "composite_or_luma_source", 0, &err);
    if (err || !RawSourcePath) {
        vsapi->mapSetError(Out, "decode_4fsc_video: composite_or_luma_source path is required");
        return;
    }

    // Get optional chroma source path (for color-under formats like VHS)
    const char *RawChromaPath = vsapi->mapGetData(In, "chroma_or_pb_source", 0, &err);
    std::filesystem::path ChromaSource;
    bool hasChromaSource = false;
    if (!err && RawChromaPath) {
        ChromaSource = RawChromaPath;
        hasChromaSource = true;
    }

    // Get optional Pr source path (for component video - not yet supported)
    const char *RawPrPath = vsapi->mapGetData(In, "pr_source", 0, &err);
    if (!err && RawPrPath) {
        // Component video mode (all 3 sources) is not yet implemented
        vsapi->mapSetError(Out, "decode_4fsc_video: component video mode (3 separate sources) is not yet supported");
        return;
    }

    std::filesystem::path Source(RawSourcePath);
    auto *D = new DecodeConfig();

    try {
        // Parse optional parameters
        D->FPSNum = vsapi->mapGetInt(In, "fpsnum", 0, &err);
        if (err)
            D->FPSNum = -1;
        D->FPSDen = vsapi->mapGetInt(In, "fpsden", 0, &err);
        if (err)
            D->FPSDen = 1;
        if (D->FPSDen < 1)
            throw VSAnalogException("FPS denominator needs to be 1 or greater");

        // Build options
        VSAnalog4fscOptions Opts;
        Opts.chromaGain = vsapi->mapGetFloat(In, "chroma_gain", 0, &err);
        if (err)
            Opts.chromaGain = 1.0;
        Opts.chromaPhase = vsapi->mapGetFloat(In, "chroma_phase", 0, &err);
        if (err)
            Opts.chromaPhase = 0.0;
        Opts.chromaNR = vsapi->mapGetFloat(In, "chroma_nr", 0, &err);
        if (err)
            Opts.chromaNR = 0.0;
        Opts.lumaNR = vsapi->mapGetFloat(In, "luma_nr", 0, &err);
        if (err)
            Opts.lumaNR = 0.0;
        Opts.paddingMultiple = static_cast<int>(vsapi->mapGetInt(In, "padding_multiple", 0, &err));
        if (err)
            Opts.paddingMultiple = 8;
        int reverseFields = vsapi->mapGetInt(In, "reverse_fields", 0, &err);
        if (err)
            reverseFields = 0;
        Opts.reverseFields = (reverseFields != 0);
        int phaseComp = vsapi->mapGetInt(In, "phase_compensation", 0, &err);
        if (err)
            phaseComp = 0;
        Opts.phaseCompensation = (phaseComp != 0);

        // Get decoder name (optional)
        const char *decoderName = vsapi->mapGetData(In, "decoder", 0, &err);
        if (!err && decoderName)
            Opts.decoder = decoderName;

        // Create the source
        D->V = std::make_unique<VSAnalog4fscSource>(
            Source,
            hasChromaSource ? &ChromaSource : nullptr,
            &Opts);

        const VSAnalogVideoProperties &VP = D->V->GetVideoProperties();

        // Validate format
        if (VP.VF.ColorFamily == 4)
            throw VSAnalogException("Unsupported source colorspace (bayer)");
        if (VP.SSModWidth == 0 || VP.SSModHeight == 0)
            throw VSAnalogException("Invalid video dimensions");

        // Set up video info
        D->VI.width = VP.SSModWidth;
        D->VI.height = VP.SSModHeight;
        D->VI.numFrames = static_cast<int>(VP.NumFrames);

        // Query the appropriate format from VapourSynth based on decoder type
        D->isMono = (VP.VF.ColorFamily == 1);
        VSColorFamily colorFamily = D->isMono ? cfGray : cfYUV;
        if (!vsapi->queryVideoFormat(&D->VI.format, colorFamily, stFloat, 32, 0, 0, Core)) {
            throw VSAnalogException(D->isMono
                ? "Failed to query GRAYS format"
                : "Failed to query YUV444PS format");
        }

        // Store video system info for frame properties
        D->isNTSC = D->V->IsNTSC();
        D->firstActiveFrameLine = D->V->GetFirstActiveFrameLine();
        auto sar = D->V->GetSAR();
        D->sarNum = sar.num;
        D->sarDen = sar.den;

        // Set frame rate
        D->VI.fpsNum = VP.FPS.Num;
        D->VI.fpsDen = VP.FPS.Den;
        vsh::reduceRational(&D->VI.fpsNum, &D->VI.fpsDen);

        // Handle custom FPS override
        if (D->FPSNum > 0) {
            vsh::reduceRational(&D->FPSNum, &D->FPSDen);
            D->VI.fpsDen = D->FPSDen;
            D->VI.fpsNum = D->FPSNum;
            D->VI.numFrames = std::max(1,
                static_cast<int>((VP.Duration * D->VI.fpsNum) * VP.TimeBase.ToDouble() / D->VI.fpsDen + 0.5));
        }

    } catch (const VSAnalogException &e) {
        delete D;
        vsapi->mapSetError(Out, (std::string("decode_4fsc_video: ") + e.what()).c_str());
        return;
    } catch (const std::exception &e) {
        delete D;
        vsapi->mapSetError(Out, (std::string("decode_4fsc_video: ") + e.what()).c_str());
        return;
    }

    // Create the video filter
    // fmUnordered because decoding is sequential (ld-decode maintains internal state)
    vsapi->createVideoFilter(Out, "decode_4fsc_video", &D->VI,
                             VSAnalog4fscSourceGetFrame, VSAnalog4fscSourceFree,
                             fmUnordered, nullptr, 0, D, Core);
}

// Plugin entry point
VS_EXTERNAL_API(void) VapourSynthPluginInit2(VSPlugin *plugin, const VSPLUGINAPI *vspapi) {
    vspapi->configPlugin(
        "com.justinarthur.vsanalog",
        "analog",
        "Functions for working with digitized analog video signals",
        VS_MAKE_VERSION(VS_ANALOG_PLUGIN_VERSION_MAJOR, VS_ANALOG_PLUGIN_VERSION_MINOR),
        VS_MAKE_VERSION(VAPOURSYNTH_API_MAJOR, 0),
        0,
        plugin
    );

    vspapi->registerFunction(
        "decode_4fsc_video",
        "composite_or_luma_source:data;"
        "chroma_or_pb_source:data:opt;"
        "pr_source:data:opt;"
        "decoder:data:opt;"
        "reverse_fields:int:opt;"
        "chroma_gain:float:opt;"
        "chroma_phase:float:opt;"
        "chroma_nr:float:opt;"
        "luma_nr:float:opt;"
        "phase_compensation:int:opt;"
        "padding_multiple:int:opt;"
        "fpsnum:int:opt;"
        "fpsden:int:opt;",
        "clip:vnode;",
        Create4fscSource,
        nullptr,
        plugin
    );
}
