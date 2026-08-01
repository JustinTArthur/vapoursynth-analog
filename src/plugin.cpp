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
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <memory>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include <VapourSynth4.h>
#include <VSHelper4.h>

// Ints per span in the AnalogDropoutSpans property: y, x_start, x_end, origin.
constexpr int kDropoutSpanStride = 4;

// A capture's SQLite sidecar wins over its JSON one on existence alone, with no
// check that it holds as much. Releases up to 0.2.3 wrote that sidecar
// themselves during a decode and left the dropout metadata out of it, so a
// capture decoded back then carries a .tbc.db that permanently hides the
// dropouts its .tbc.json still lists — from correction and annotation alike,
// and without complaint. Nothing writes those sidecars now, but the ones
// already on disk keep answering first, so say so rather than report a damaged
// capture as clean.
//
// SQLite keeps its schema as the plain text of the statements that built it, so
// searching the file for the table beats linking a SQL parser in to ask. Read
// in overlapping chunks: the schema sits wherever its pages were allocated, not
// necessarily near the front.
bool sidecarOmitsDropouts(const std::filesystem::path &source) {
    std::filesystem::path db = source;
    db += ".db";
    std::filesystem::path json = source;
    json += ".json";
    std::error_code ec;
    // Only the pairing is a problem: a .db with no .json beside it is all the
    // metadata there is, and nothing better is being shadowed.
    if (!std::filesystem::exists(db, ec) || !std::filesystem::exists(json, ec))
        return false;

    std::ifstream in(db, std::ios::binary);
    if (!in)
        return false;

    static constexpr std::string_view kTable = "drop_outs";
    static constexpr size_t kChunk = 1u << 20;
    const size_t overlap = kTable.size() - 1;
    std::string buffer(kChunk + overlap, '\0');
    size_t carried = 0;
    while (in.read(buffer.data() + carried, static_cast<std::streamsize>(kChunk)) ||
           in.gcount() > 0) {
        const size_t filled = carried + static_cast<size_t>(in.gcount());
        if (std::string_view(buffer.data(), filled).find(kTable) != std::string_view::npos)
            return false;
        carried = std::min(filled, overlap);
        std::memmove(buffer.data(), buffer.data() + filled - carried, carried);
    }
    return true;
}

// Warn for every capture whose dropouts a stale sidecar is swallowing. Only
// worth saying when the decode was going to act on them; a plain decode is
// unaffected by what the sidecar left out.
void warnIfDropoutsHidden(const std::vector<std::filesystem::path> &sources,
                          VSCore *core, const VSAPI *vsapi) {
    for (const auto &source : sources) {
        if (!sidecarOmitsDropouts(source))
            continue;
        const std::string message =
            "decode_4fsc_video: " + source.filename().string() + ".db has no dropout "
            "metadata and takes precedence over " + source.filename().string() +
            ".json, which does. No dropout will be corrected or reported for this "
            "source. The .db was written by vapoursynth-analog 0.2.3 or earlier and "
            "nothing needs it now: move or delete it to decode from the .json.";
        vsapi->logMessage(mtWarning, message.c_str(), core);
    }
}

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

    // Dropout regions as one flat (y, x_start, x_end, origin) run per span,
    // rather than four parallel arrays: a stride of 4 means the array length is
    // never 1 (would cause VS Python layer to hand a 1-element array back as
    // a bare int instead of a list. Property is present but empty on a clean
    // frame, informing create_dropouts_mask the clip was annotated at all.
    if (extra.hasDropoutSpans) {
        std::vector<int64_t> flat;
        flat.reserve(extra.dropoutSpans.size() * kDropoutSpanStride);
        for (const auto &span : extra.dropoutSpans) {
            flat.push_back(span.y);
            flat.push_back(span.x_start);
            flat.push_back(span.x_end);
            flat.push_back(span.origin);
        }
        // mapSetIntArray reads nothing at size 0, but never hand it a null.
        static constexpr int64_t empty = 0;
        vsapi->mapSetIntArray(props, "AnalogDropoutSpans",
                              flat.empty() ? &empty : flat.data(),
                              static_cast<int>(flat.size()));
    }

    return dst;
}

static void VS_CC VSAnalog4fscSourceFree(void *instanceData, VSCore *, const VSAPI *) {
    delete static_cast<DecodeConfig *>(instanceData);
}

// Rasterises the AnalogDropoutSpans property into a mask clip.
struct DropoutMaskConfig {
    VSNode *node = nullptr;
    VSVideoInfo VI = {};
    // Span origins to draw; empty draws every origin.
    std::vector<int64_t> origins;

    bool wanted(int64_t origin) const {
        if (origins.empty()) return true;
        return std::find(origins.begin(), origins.end(), origin) != origins.end();
    }
};

// Set every sample of a mask row in [xStart, xEnd) to the "dropped" value.
static void fillMaskRun(uint8_t *row, int xStart, int xEnd, const VSVideoFormat &fmt) {
    if (fmt.sampleType == stFloat) {
        auto *p = reinterpret_cast<float *>(row);
        std::fill(p + xStart, p + xEnd, 1.0f);
    } else if (fmt.bytesPerSample == 1) {
        std::fill(row + xStart, row + xEnd, static_cast<uint8_t>((1u << fmt.bitsPerSample) - 1));
    } else {
        auto *p = reinterpret_cast<uint16_t *>(row);
        std::fill(p + xStart, p + xEnd, static_cast<uint16_t>((1u << fmt.bitsPerSample) - 1));
    }
}

static const VSFrame *VS_CC DropoutMaskGetFrame(
    int n, int activationReason, void *instanceData, void **,
    VSFrameContext *frameCtx, VSCore *core, const VSAPI *vsapi
) {
    auto *D = static_cast<DropoutMaskConfig *>(instanceData);

    if (activationReason == arInitial) {
        vsapi->requestFrameFilter(n, D->node, frameCtx);
        return nullptr;
    }
    if (activationReason != arAllFramesReady) {
        return nullptr;
    }

    const VSFrame *src = vsapi->getFrameFilter(n, D->node, frameCtx);
    const VSMap *srcProps = vsapi->getFramePropertiesRO(src);

    // -1 is an absent property, 0 a present-but-empty one: the clip was
    // annotated and this frame simply has no dropouts.
    const int numElements = vsapi->mapNumElements(srcProps, "AnalogDropoutSpans");
    if (numElements < 0) {
        vsapi->freeFrame(src);
        vsapi->setFilterError(
            "create_dropouts_mask: clip has no AnalogDropoutSpans property; decode it "
            "with analog.decode_4fsc_video(annotate_dropouts=1)", frameCtx);
        return nullptr;
    }
    if (numElements % kDropoutSpanStride != 0) {
        vsapi->freeFrame(src);
        vsapi->setFilterError(
            "create_dropouts_mask: AnalogDropoutSpans length is not a multiple of 4",
            frameCtx);
        return nullptr;
    }

    // Carries _FieldBased, the SAR and the durations onto the mask; the colour
    // props copied with them are inert on a single-plane clip.
    VSFrame *dst = vsapi->newVideoFrame(&D->VI.format, D->VI.width, D->VI.height, src, core);
    if (!dst) {
        vsapi->freeFrame(src);
        vsapi->setFilterError("create_dropouts_mask: failed to allocate output frame", frameCtx);
        return nullptr;
    }

    auto *maskData = vsapi->getWritePtr(dst, 0);
    const ptrdiff_t maskStride = vsapi->getStride(dst, 0);
    // Clean is zero in every supported format, padding included.
    std::memset(maskData, 0, static_cast<size_t>(maskStride) * D->VI.height);

    const int64_t *spans = numElements > 0
        ? vsapi->mapGetIntArray(srcProps, "AnalogDropoutSpans", nullptr)
        : nullptr;
    for (int i = 0; i < numElements; i += kDropoutSpanStride) {
        const int64_t y = spans[i];
        if (!D->wanted(spans[i + 3])) continue;
        if (y < 0 || y >= D->VI.height) continue;
        // libchromadec clips spans to the output framing, so this only guards
        // against a hand-edited property.
        const auto xStart = static_cast<int>(std::clamp<int64_t>(spans[i + 1], 0, D->VI.width));
        const auto xEnd = static_cast<int>(std::clamp<int64_t>(spans[i + 2], 0, D->VI.width));
        if (xEnd <= xStart) continue;
        fillMaskRun(maskData + y * maskStride, xStart, xEnd, D->VI.format);
    }

    VSMap *dstProps = vsapi->getFramePropertiesRW(dst);
    // A mask occupies the whole 0..1 scale, unlike the matrix-derived clip it
    // came from.
    vsapi->mapSetInt(dstProps, "_ColorRange", 0, maReplace);
    vsapi->mapSetInt(dstProps, "_Range", 1, maReplace);
    // Inherited from a YUV/RGB parent, these describe a colour encoding the
    // mask does not have.
    vsapi->mapDeleteKey(dstProps, "_Matrix");
    vsapi->mapDeleteKey(dstProps, "_Primaries");
    vsapi->mapDeleteKey(dstProps, "_ChromaLocation");

    vsapi->freeFrame(src);
    return dst;
}

static void VS_CC DropoutMaskFree(void *instanceData, VSCore *, const VSAPI *vsapi) {
    auto *D = static_cast<DropoutMaskConfig *>(instanceData);
    vsapi->freeNode(D->node);
    delete D;
}

static void VS_CC CreateDropoutsMask(const VSMap *In, VSMap *Out, void *, VSCore *Core, const VSAPI *vsapi) {
    auto *D = new DropoutMaskConfig();
    D->node = vsapi->mapGetNode(In, "clip", 0, nullptr);
    D->VI = *vsapi->getVideoInfo(D->node);

    if (!vsh::isConstantVideoFormat(&D->VI)) {
        vsapi->mapSetError(Out, "create_dropouts_mask: clip must have a constant format");
        DropoutMaskFree(D, Core, vsapi);
        return;
    }

    // Match the clip's precision so the mask drops straight into
    // std.MaskedMerge, which requires an equal bit depth.
    const VSVideoFormat srcFormat = D->VI.format;
    const bool supported = (srcFormat.sampleType == stFloat && srcFormat.bitsPerSample == 32) ||
                           (srcFormat.sampleType == stInteger && srcFormat.bitsPerSample >= 8 &&
                            srcFormat.bitsPerSample <= 16);
    if (!supported) {
        vsapi->mapSetError(
            Out, "create_dropouts_mask: clip must be 8-16 bit integer or 32-bit float");
        DropoutMaskFree(D, Core, vsapi);
        return;
    }
    if (!vsapi->queryVideoFormat(&D->VI.format, cfGray, srcFormat.sampleType,
                                 srcFormat.bitsPerSample, 0, 0, Core)) {
        vsapi->mapSetError(Out, "create_dropouts_mask: failed to query mask format");
        DropoutMaskFree(D, Core, vsapi);
        return;
    }

    int err;
    const int numOrigins = vsapi->mapNumElements(In, "origins");
    for (int i = 0; i < numOrigins; i++) {
        const int64_t origin = vsapi->mapGetInt(In, "origins", i, &err);
        if (!err) D->origins.push_back(origin);
    }

    VSFilterDependency deps[] = {{D->node, rpStrictSpatial}};
    vsapi->createVideoFilter(Out, "create_dropouts_mask", &D->VI,
                             DropoutMaskGetFrame, DropoutMaskFree,
                             fmParallel, deps, 1, D, Core);
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
        int annotateDropouts = static_cast<int>(vsapi->mapGetInt(In, "annotate_dropouts", 0, &err));
        if (err) annotateDropouts = 0;
        Opts.annotateDropouts = (annotateDropouts != 0);

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

        if (Opts.dropoutCorrect || Opts.annotateDropouts) {
            std::vector<std::filesystem::path> dropoutSources{Source};
            if (hasChromaSource) dropoutSources.push_back(ChromaSource);
            dropoutSources.insert(dropoutSources.end(),
                                  Opts.dropoutExtraLumaSources.begin(),
                                  Opts.dropoutExtraLumaSources.end());
            dropoutSources.insert(dropoutSources.end(),
                                  Opts.dropoutExtraChromaSources.begin(),
                                  Opts.dropoutExtraChromaSources.end());
            warnIfDropoutsHidden(dropoutSources, Core, vsapi);
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
        "annotate_dropouts:int:opt;",
        "clip:vnode;",
        Create4fscSource,
        nullptr,
        plugin
    );

    vspapi->registerFunction(
        "create_dropouts_mask",
        "clip:vnode;"
        "origins:int[]:opt;",
        "clip:vnode;",
        CreateDropoutsMask,
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
