/******************************************************************************
 * plugin.cpp
 * vapoursynth-analog - VapourSynth plugin entry point
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 ******************************************************************************/

#include "version.h"
#include "analog4fsc.h"
#include "chdlog.h"

#include <algorithm>
#include <cctype>
#include <filesystem>
#include <memory>
#include <string>
#include <utility>

#include <VapourSynth4.h>
#include <VSHelper4.h>

// Holds libchromadec's diagnostics on this core's log for the instance's
// lifetime. Both the construction error path and the free callback destroy the
// DecodeConfig, so one member covers them.
struct LogAttachment {
    VSCore *core = nullptr;
    ~LogAttachment() { if (core) chdlog::detach(core); }
};

// Decode configuration data passed to filter callbacks.
struct DecodeConfig {
    // First member, so it outlives V: tearing the decoder down is itself a
    // libchromadec call that can log.
    LogAttachment logAttachment;
    VSVideoInfo VI = {};
    std::unique_ptr<VSAnalog4fscSource> V;
    int64_t FPSNum = -1;
    int64_t FPSDen = -1;
    int numPlanes = 3;
    bool isRGB = false;               // RGB output → _Matrix=0
    bool isNTSCChromaticity = false;  // NTSC / PAL-M
    bool classicColorDifference = false;  // NTSC-1953 luma matrix → _Matrix=4
    bool isSubsampledChroma = false;  // 4:4:0 SECAM → _ChromaLocation
    bool bottomFieldFirst = false;
    int firstActiveSample = 0;
    int lastActiveSample = 0;
    int firstActiveLine = 0;
    int lastActiveLine = 0;
    int sarNum = 1;
    int sarDen = 1;
    bool dropoutCorrect = false;
};

// Helper: fetch an optional string parameter (returns "" when absent).
static std::string getOptString(const VSMap *in, const char *key, const VSAPI *vsapi) {
    int err;
    const char *v = vsapi->mapGetData(in, key, 0, &err);
    if (err || !v) return {};
    return std::string(v);
}

// Frame getter callback.
static const VSFrame *VS_CC VSAnalog4fscSourceGetFrame(
    int n, int activationReason, void *instanceData, void **,
    VSFrameContext *frameCtx, VSCore *core, const VSAPI *vsapi
) {
    auto *D = static_cast<DecodeConfig *>(instanceData);

    if (activationReason != arInitial) {
        return nullptr;
    }

    VSFrame *dst = vsapi->newVideoFrame(&D->VI.format, D->VI.width, D->VI.height, nullptr, core);
    if (!dst) {
        vsapi->setFilterError("Failed to allocate output frame", frameCtx);
        return nullptr;
    }

    float *planeData[3] = {nullptr, nullptr, nullptr};
    int planeStride[3] = {0, 0, 0};
    for (int p = 0; p < D->numPlanes; p++) {
        planeData[p] = reinterpret_cast<float *>(vsapi->getWritePtr(dst, p));
        planeStride[p] = static_cast<int>(vsapi->getStride(dst, p));
    }

    VSAnalog4fscSource::FrameExtra extra;
    try {
        D->V->GetFrame(n, planeData, planeStride, D->numPlanes, extra);
    } catch (const std::exception &e) {
        vsapi->freeFrame(dst);
        vsapi->setFilterError(e.what(), frameCtx);
        return nullptr;
    }

    VSMap *props = vsapi->getFramePropertiesRW(dst);

    // Color primaries / matrix / transfer.
    // NTSC / PAL-M: SMPTE ST 170 (_Primaries=6, _Matrix=6).
    // PAL / SECAM (625-line): ITU-T BT.470 BG (_Primaries=5, _Matrix=5).
    // The classic color-difference precision demodulates against the NTSC-1953
    // luma matrix, which H.273 signals as _Matrix=4 whatever the primaries are.
    // RGB output carries no luma matrix (_Matrix=0, identity).
    int matrix;
    if (D->isRGB) {
        matrix = 0;
    } else if (D->classicColorDifference) {
        matrix = 4;
    } else {
        matrix = D->isNTSCChromaticity ? 6 : 5;
    }
    vsapi->mapSetInt(props, "_Primaries", D->isNTSCChromaticity ? 6 : 5, maReplace);
    vsapi->mapSetInt(props, "_Matrix", matrix, maReplace);
    vsapi->mapSetInt(props, "_Transfer", 1, maReplace);

    // Matrix-derived float samples map to the limited-range integer scales even
    // though they occupy the full float range; mark limited so downstream
    // conversions to integer Y′CbCr stay limited without user intervention.
    vsapi->mapSetInt(props, "_ColorRange", 1, maReplace);
    vsapi->mapSetInt(props, "_Range", 0, maReplace);

    // Field order. The 486-line 525-system window starts on a field 2 half-line
    // and so is bottom first; the 576-line 625-system window starts on a field 1
    // line.
    vsapi->mapSetInt(props, "_FieldBased", D->bottomFieldFirst ? 1 : 2, maReplace);

    // 4:4:0 chroma siting. No single value is honest for the SECAM lattice —
    // each plane alternates between the first and the second luma row of its
    // pair — but top-left matches the plane whose first row is the first
    // active line, and tells naive consumers not to assume centred siting.
    if (D->isSubsampledChroma) {
        vsapi->mapSetInt(props, "_ChromaLocation", 2, maReplace);
    }

    vsapi->mapSetInt(props, "_SARNum", D->sarNum, maReplace);
    vsapi->mapSetInt(props, "_SARDen", D->sarDen, maReplace);

    // Constant frame rate → invert fps for per-frame duration.
    vsapi->mapSetInt(props, "_DurationNum", D->VI.fpsDen, maReplace);
    vsapi->mapSetInt(props, "_DurationDen", D->VI.fpsNum, maReplace);

    // Active-region reporting in the interface standards' inclusive numbering:
    // samples from the start of the digital active line (ST 244 / EBU Tech
    // 3280-E), lines as field-sequential signal numbers (ST 170 / BT.470 /
    // BT.1700). The frame is exactly this window, so pixel (0, 0) is sample
    // AnalogFirstActiveSample of line AnalogFirstActiveLine. The line
    // numbering runs field by field rather than down the raster, so on
    // a 525-line window the first line's number is the larger of the two.
    vsapi->mapSetInt(props, "AnalogFirstActiveSample", D->firstActiveSample, maReplace);
    vsapi->mapSetInt(props, "AnalogLastActiveSample", D->lastActiveSample, maReplace);
    vsapi->mapSetInt(props, "AnalogFirstActiveLine", D->firstActiveLine, maReplace);
    vsapi->mapSetInt(props, "AnalogLastActiveLine", D->lastActiveLine, maReplace);

    // SECAM: which color-difference component the first chroma row carries.
    // Flips frame-to-frame (odd 625-line count); lets downstream filters align
    // and resample the 4:4:0 chroma against luma.
    if (extra.hasSecamComponent) {
        vsapi->mapSetData(props, "AnalogSecamFirstRowComponent",
                          extra.secamFirstRowComponent == 1 ? "Dr" : "Db", -1,
                          dtUtf8, maReplace);
    }

    if (extra.hasDropoutStats) {
        vsapi->mapSetInt(props, "AnalogDropoutsCorrected", extra.dropoutCorrected, maReplace);
        vsapi->mapSetInt(props, "AnalogDropoutsFailed", extra.dropoutFailed, maReplace);
        vsapi->mapSetInt(props, "AnalogDropoutsTotalDistance", extra.dropoutTotalDistance, maReplace);
    }

    return dst;
}

static void VS_CC VSAnalog4fscSourceFree(void *instanceData, VSCore *, const VSAPI *) {
    delete static_cast<DecodeConfig *>(instanceData);
}

// Threshold for the decoder's own diagnostics, which arrive as core log
// messages. Process-global in libchromadec, so this is a plugin-level knob
// rather than a per-clip argument.
static void VS_CC SetLogLevel(const VSMap *In, VSMap *Out, void *, VSCore *, const VSAPI *vsapi) {
    int err;
    const char *rawLevel = vsapi->mapGetData(In, "level", 0, &err);
    if (err || !rawLevel) {
        vsapi->mapSetError(Out, "set_log_level: level is required");
        return;
    }

    std::string level(rawLevel);
    std::transform(level.begin(), level.end(), level.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });

    if (level == "debug") {
        chdlog::setLevel(CHD_LOG_DEBUG);
    } else if (level == "info") {
        chdlog::setLevel(CHD_LOG_INFO);
    } else if (level == "warning" || level == "warn") {
        chdlog::setLevel(CHD_LOG_WARN);
    } else if (level == "critical" || level == "error") {
        chdlog::setLevel(CHD_LOG_ERROR);
    } else if (level == "off" || level == "none") {
        chdlog::setLevel(CHD_LOG_OFF);
    } else {
        vsapi->mapSetError(
            Out, ("set_log_level: unknown level '" + level +
                  "'; expected debug, info, warning, critical or off").c_str());
    }
}

static void VS_CC Create4fscSource(const VSMap *In, VSMap *Out, void *, VSCore *Core, const VSAPI *vsapi) {
    int err;

    const char *RawSourcePath = vsapi->mapGetData(In, "composite_or_luma_source", 0, &err);
    if (err || !RawSourcePath) {
        vsapi->mapSetError(Out, "decode_4fsc_video: composite_or_luma_source path is required");
        return;
    }

    const char *RawChromaPath = vsapi->mapGetData(In, "chroma_or_pb_source", 0, &err);
    std::filesystem::path ChromaSource;
    bool hasChromaSource = false;
    if (!err && RawChromaPath) {
        ChromaSource = RawChromaPath;
        hasChromaSource = true;
    }

    const char *RawPrPath = vsapi->mapGetData(In, "pr_source", 0, &err);
    if (!err && RawPrPath) {
        vsapi->mapSetError(Out, "decode_4fsc_video: component video mode (3 separate sources) is not yet supported");
        return;
    }

    std::filesystem::path Source(RawSourcePath);
    auto *D = new DecodeConfig();

    // Before the first libchromadec call, so open- and commit-time warnings
    // reach the log too.
    chdlog::attach(Core, vsapi);
    D->logAttachment.core = Core;

    try {
        D->FPSNum = vsapi->mapGetInt(In, "fpsnum", 0, &err);
        if (err) D->FPSNum = -1;
        D->FPSDen = vsapi->mapGetInt(In, "fpsden", 0, &err);
        if (err) D->FPSDen = 1;
        if (D->FPSDen < 1)
            throw VSAnalogException("FPS denominator needs to be 1 or greater");

        VSAnalog4fscOptions Opts;
        Opts.decoder = getOptString(In, "decoder", vsapi);
        Opts.colorFamily = getOptString(In, "color_family", vsapi);
        Opts.chromaFilter = getOptString(In, "chroma_filter", vsapi);
        Opts.colorDiffPrecision = getOptString(In, "color_difference_precision", vsapi);
        Opts.broadcastScalingPrecision = getOptString(In, "broadcast_scaling_precision", vsapi);

        Opts.chromaGain = vsapi->mapGetFloat(In, "chroma_gain", 0, &err);
        if (err) Opts.chromaGain = 1.0;
        Opts.chromaPhase = vsapi->mapGetFloat(In, "chroma_phase", 0, &err);
        if (err) Opts.chromaPhase = 0.0;
        Opts.chromaNR = vsapi->mapGetFloat(In, "chroma_nr", 0, &err);
        if (err) Opts.chromaNR = 0.0;
        Opts.lumaNR = vsapi->mapGetFloat(In, "luma_nr", 0, &err);
        if (err) Opts.lumaNR = 0.0;
        // Crop bounds are optional individually, and every integer is a
        // meaningful value (a sample number may be negative), so absence is
        // read from the error flag rather than a sentinel.
        for (auto [key, bound] : {
                 std::pair{"first_active_sample", &Opts.firstActiveSample},
                 std::pair{"last_active_sample", &Opts.lastActiveSample},
                 std::pair{"first_active_line", &Opts.firstActiveLine},
                 std::pair{"last_active_line", &Opts.lastActiveLine}}) {
            const auto value = static_cast<int>(vsapi->mapGetInt(In, key, 0, &err));
            if (!err) *bound = value;
        }
        int reverseFields = static_cast<int>(vsapi->mapGetInt(In, "reverse_fields", 0, &err));
        if (err) reverseFields = 0;
        Opts.reverseFields = (reverseFields != 0);
        int phaseComp = static_cast<int>(vsapi->mapGetInt(In, "phase_compensation", 0, &err));
        if (err) phaseComp = 1;
        Opts.phaseCompensation = (phaseComp != 0);

        // Neural-network options.
        Opts.modelPath = getOptString(In, "model_path", vsapi);
        Opts.onnxProvider = getOptString(In, "onnx_provider", vsapi);
        int modelBandpass = static_cast<int>(vsapi->mapGetInt(In, "model_chroma_bandpass", 0, &err));
        if (err) modelBandpass = 1;
        Opts.modelChromaBandpass = (modelBandpass != 0);
        Opts.modelInputScale = vsapi->mapGetFloat(In, "model_input_scale", 0, &err);
        if (err || Opts.modelInputScale <= 0.0) Opts.modelInputScale = 1.0;

        // Dropout correction.
        int dropoutCorrect = static_cast<int>(vsapi->mapGetInt(In, "dropout_correct", 0, &err));
        if (err) dropoutCorrect = 0;
        Opts.dropoutCorrect = (dropoutCorrect != 0);
        int dropoutOvercorrect = static_cast<int>(vsapi->mapGetInt(In, "dropout_overcorrect", 0, &err));
        if (err) dropoutOvercorrect = 0;
        Opts.dropoutOvercorrect = (dropoutOvercorrect != 0);
        int dropoutIntra = static_cast<int>(vsapi->mapGetInt(In, "dropout_intra", 0, &err));
        if (err) dropoutIntra = 0;
        Opts.dropoutIntra = (dropoutIntra != 0);

        int numExtraLuma = vsapi->mapNumElements(In, "dropout_composite_or_luma_extra_sources");
        for (int i = 0; i < numExtraLuma; i++) {
            const char *path = vsapi->mapGetData(In, "dropout_composite_or_luma_extra_sources", i, &err);
            if (!err && path) Opts.dropoutExtraLumaSources.emplace_back(path);
        }
        int numExtraChroma = vsapi->mapNumElements(In, "dropout_chroma_extra_sources");
        for (int i = 0; i < numExtraChroma; i++) {
            const char *path = vsapi->mapGetData(In, "dropout_chroma_extra_sources", i, &err);
            if (!err && path) Opts.dropoutExtraChromaSources.emplace_back(path);
        }

        D->V = std::make_unique<VSAnalog4fscSource>(
            Source, hasChromaSource ? &ChromaSource : nullptr, &Opts);

        // Map the resolved output family to a VapourSynth format.
        VSColorFamily colorFamily;
        int subSamplingH = 0;
        switch (D->V->GetVSFormat()) {
            case VSAnalog4fscSource::VSFormat::Gray:
                colorFamily = cfGray;
                D->numPlanes = 1;
                break;
            case VSAnalog4fscSource::VSFormat::RGB:
                colorFamily = cfRGB;
                D->numPlanes = 3;
                D->isRGB = true;
                break;
            case VSAnalog4fscSource::VSFormat::YUV440:
                colorFamily = cfYUV;
                D->numPlanes = 3;
                subSamplingH = 1;  // 4:4:0
                D->isSubsampledChroma = true;
                break;
            default:  // YUV444
                colorFamily = cfYUV;
                D->numPlanes = 3;
                break;
        }
        if (!vsapi->queryVideoFormat(&D->VI.format, colorFamily, stFloat, 32, 0, subSamplingH, Core)) {
            throw VSAnalogException("Failed to query output video format");
        }

        D->VI.width = D->V->GetWidth();
        D->VI.height = D->V->GetHeight();
        D->VI.numFrames = static_cast<int>(D->V->GetNumFrames());
        if (D->VI.width <= 0 || D->VI.height <= 0)
            throw VSAnalogException("Invalid video dimensions");

        D->dropoutCorrect = Opts.dropoutCorrect;
        D->isNTSCChromaticity = D->V->IsNTSCChromaticity();
        D->classicColorDifference = D->V->UsesClassicColorDifference();
        D->bottomFieldFirst = D->V->IsBottomFieldFirst();
        D->firstActiveSample = D->V->GetFirstActiveSample();
        D->lastActiveSample = D->V->GetLastActiveSample();
        D->firstActiveLine = D->V->GetFirstActiveLine();
        D->lastActiveLine = D->V->GetLastActiveLine();
        auto sar = D->V->GetSAR();
        D->sarNum = sar.num;
        D->sarDen = sar.den;

        auto fps = D->V->GetFPS();
        D->VI.fpsNum = fps.Num;
        D->VI.fpsDen = fps.Den;
        vsh::reduceRational(&D->VI.fpsNum, &D->VI.fpsDen);

        // Custom FPS override.
        if (D->FPSNum > 0) {
            vsh::reduceRational(&D->FPSNum, &D->FPSDen);
            auto timeBase = D->V->GetTimeBase();
            D->VI.fpsDen = D->FPSDen;
            D->VI.fpsNum = D->FPSNum;
            D->VI.numFrames = std::max(1,
                static_cast<int>((D->V->GetDuration() * D->VI.fpsNum) *
                                 timeBase.ToDouble() / D->VI.fpsDen + 0.5));
        }

    } catch (const std::exception &e) {
        delete D;
        vsapi->mapSetError(Out, (std::string("decode_4fsc_video: ") + e.what()).c_str());
        return;
    }

    // fmUnordered: a single libchromadec decoder can't take concurrent calls.
    vsapi->createVideoFilter(Out, "decode_4fsc_video", &D->VI,
                             VSAnalog4fscSourceGetFrame, VSAnalog4fscSourceFree,
                             fmUnordered, nullptr, 0, D, Core);
}

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
        "color_family:data:opt;"
        "chroma_filter:data:opt;"
        "color_difference_precision:data:opt;"
        "broadcast_scaling_precision:data:opt;"
        "reverse_fields:int:opt;"
        "chroma_gain:float:opt;"
        "chroma_phase:float:opt;"
        "chroma_nr:float:opt;"
        "luma_nr:float:opt;"
        "phase_compensation:int:opt;"
        "first_active_sample:int:opt;"
        "last_active_sample:int:opt;"
        "first_active_line:int:opt;"
        "last_active_line:int:opt;"
        "model_path:data:opt;"
        "onnx_provider:data:opt;"
        "model_chroma_bandpass:int:opt;"
        "model_input_scale:float:opt;"
        "dropout_correct:int:opt;"
        "dropout_overcorrect:int:opt;"
        "dropout_intra:int:opt;"
        "dropout_composite_or_luma_extra_sources:data[]:opt;"
        "dropout_chroma_extra_sources:data[]:opt;"
        "fpsnum:int:opt;"
        "fpsden:int:opt;",
        "clip:vnode;",
        Create4fscSource,
        nullptr,
        plugin
    );

    vspapi->registerFunction(
        "set_log_level",
        "level:data;",
        "",
        SetLogLevel,
        nullptr,
        plugin
    );
}
