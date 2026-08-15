/******************************************************************************
 * analog4fsc.cpp
 * vapoursynth-analog - 4𝑓𝑠𝑐 video source for VapourSynth
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 ******************************************************************************/

#include "analog4fsc.h"

#include <cstring>
#include <string>

namespace {

#if defined(__APPLE__)
constexpr bool kIsDarwin = true;
#else
constexpr bool kIsDarwin = false;
#endif

// Map a decoder name (as used by the plugin/CLI) to a libchromadec kind.
chd_decoder_kind_t parseDecoder(const std::string &name) {
    if (name.empty() || name == "auto") return CHD_DEC_AUTO;
    if (name == "mono") return CHD_DEC_MONO;
    if (name == "ntsc1d") return CHD_DEC_NTSC_1D;
    if (name == "ntsc2d") return CHD_DEC_NTSC_2D;
    if (name == "ntsc3d") return CHD_DEC_NTSC_3D;
    if (name == "ntsc3dnoadapt") return CHD_DEC_NTSC_3D_NO_ADAPT;
    if (name == "pal2d") return CHD_DEC_PAL_2D;
    if (name == "transform2d") return CHD_DEC_TRANSFORM_2D;
    if (name == "transform3d") return CHD_DEC_TRANSFORM_3D;
    if (name == "nntransform3d") return CHD_DEC_NN_TRANSFORM3D;
    if (name == "ldzeug2_color_cnn") return CHD_DEC_LDZEUG_COLOR_CNN;
    if (name == "ldzeug2_luma_sep") return CHD_DEC_LDZEUG_LUMA_SEP;
    if (name == "ldzeug2_luma_sep_frame") return CHD_DEC_LDZEUG_LUMA_SEP_FRAME;
    if (name == "secam") return CHD_DEC_SECAM;
    throw VSAnalogException("Unknown decoder: " + name);
}

bool isNnDecoder(chd_decoder_kind_t kind) {
    return kind == CHD_DEC_NN_TRANSFORM3D || kind == CHD_DEC_LDZEUG_COLOR_CNN ||
           kind == CHD_DEC_LDZEUG_LUMA_SEP || kind == CHD_DEC_LDZEUG_LUMA_SEP_FRAME;
}

// Map an execution-provider name to a libchromadec NN backend. Values are
// pre-validated in the Python wrapper; unknown names fall back to AUTO.
chd_nn_backend_t providerToBackend(const std::string &p) {
    if (p.empty() || p == "auto") return CHD_NN_BACKEND_AUTO;
    if (p == "cpu") return CHD_NN_ORT_CPU;
    if (p == "cuda" || p == "gpu") return CHD_NN_ORT_CUDA;
    if (p == "tensorrt" || p == "trt") return CHD_NN_ORT_TENSORRT;
    if (p == "migraphx") return CHD_NN_ORT_MIGRAPHX;
    if (p == "directml") return CHD_NN_ORT_DIRECTML;
    if (p == "coreml") return kIsDarwin ? CHD_NN_COREML : CHD_NN_ORT_COREML;
    return CHD_NN_BACKEND_AUTO;
}

// Map a compute-precision name to a libchromadec NN session precision. fp16 is
// permission rather than a guarantee: only a backend that compiles the model
// into a device engine acts on it (TensorRT, via trt_fp16_enable), and the rest
// run the model at its stored precision. Rejected rather than ignored — a typo
// would otherwise read as an fp16 run that quietly measured fp32.
chd_nn_compute_precision_t parsePrecision(const std::string &p) {
    if (p.empty() || p == "fp32") return CHD_NN_PRECISION_FP32;
    if (p == "fp16") return CHD_NN_PRECISION_FP16_ALLOWED;
    throw VSAnalogException("Unknown model_precision: " + p +
                            " (expected \"fp32\" or \"fp16\")");
}

// The active window of an interface standard, inclusive, in that standard's own
// numbering: samples from the start of the digital active line, lines as
// field-sequential signal numbers. The sample window is the digital active
// line, which overhangs the picture on both sides and so carries the blanking
// transitions.
struct ActiveWindow {
    int firstSample;
    int lastSample;
    int firstLine;
    int lastLine;
};

// 625-line: EBU Tech 3280-E's 948-sample digital active line, and BT.1700's
// 576-line active picture — field 1 lines 23-310 and field 2 lines 336-623,
// which weave into one contiguous block running from line 23 down to line 623.
// SECAM shares it.
//
// 525-line: SMPTE ST 244's 768-sample digital active line, and ST 170's
// 486-line active picture:  field 1 lines 21-263 and field 2 lines 283-525.
// Field 2 starts at line 264, so its first active line sits half a line *above*
// field 1's: the window's top line is field 2's 283 and its bottom is field 1's
// 263, and 486-line output is therefore bottom field first. PAL-M shares the
// window; its own digital active line is one sample further left, which the
// per-source conversion takes care of.
ActiveWindow standardActiveWindow(chd_video_standard_t standard) {
    if (standard == CHD_STD_PAL || standard == CHD_STD_SECAM) {
        return {0, 947, 23, 623};
    }
    return {0, 767, 283, 263};
}

}  // namespace

VSAnalog4fscSource::VSAnalog4fscSource(const std::filesystem::path &sourcePath,
                                       const std::filesystem::path *chromaSourcePath,
                                       const VSAnalog4fscOptions *opts)
    : src(std::make_unique<ChromaDecSource>()) {
    configure(sourcePath, chromaSourcePath, opts);
}

VSAnalog4fscSource::~VSAnalog4fscSource() = default;

void VSAnalog4fscSource::configure(const std::filesystem::path &sourcePath,
                                   const std::filesystem::path *chromaSourcePath,
                                   const VSAnalog4fscOptions *opts) {
    static const VSAnalog4fscOptions defaults;
    if (!opts) opts = &defaults;

    // Force SECAM over a 625-line sidecar that mis-declares PAL when the user
    // explicitly asks for the SECAM decoder.
    chd_video_params_t override = {};
    const chd_video_params_t *overridePtr = nullptr;
    if (opts->decoder == "secam") {
        override.standard = CHD_STD_SECAM;
        overridePtr = &override;
    }

    // Open (libchromadec detects .tbc vs CVBS .cvbs/.cvbsy/.cvbsc by extension).
    const bool dual = (chromaSourcePath != nullptr);
    if (dual) {
        src->openYC(sourcePath, *chromaSourcePath, overridePtr);
    } else {
        src->openComposite(sourcePath, overridePtr);
    }

    // Extra sources for multi-source dropout correction.
    for (const auto &extra : opts->dropoutExtraLumaSources) {
        src->addExtraComposite(extra);
    }
    for (const auto &extra : opts->dropoutExtraChromaSources) {
        src->addExtraComposite(extra);
    }

    const chd_video_info_t &vinfo = src->info();
    isSecam = (vinfo.standard == CHD_STD_SECAM);
    isNtscChromaticity =
        (vinfo.standard == CHD_STD_NTSC || vinfo.standard == CHD_STD_PAL_M);
    isWidescreen = (vinfo.is_widescreen != 0);

    // Pin the active window to the interface standard rather than inheriting
    // whatever crop the source declares, so a given system always decodes to
    // the same raster. Each bound can be overridden independently, in the
    // standard's own numbering; libchromadec rotates and weaves them into the
    // source's row coordinates, so the same numbers land on the same picture
    // whatever alignment and field height the capture uses.
    const ActiveWindow win = standardActiveWindow(vinfo.standard);
    firstActiveSample = opts->firstActiveSample.value_or(win.firstSample);
    lastActiveSample = opts->lastActiveSample.value_or(win.lastSample);
    firstActiveLine = opts->firstActiveLine.value_or(win.firstLine);
    lastActiveLine = opts->lastActiveLine.value_or(win.lastLine);

    // A negative sample number counts back from the start of the digital active
    // line, into the line blanking ahead of it, which is the far end of the same
    // row: normalise it into the standard's own 0..field_width-1 numbering.
    for (int *sample : {&firstActiveSample, &lastActiveSample}) {
        if (*sample <= -vinfo.field_width || *sample >= vinfo.field_width) {
            throw VSAnalogException(
                "Active sample " + std::to_string(*sample) + " is more than one " +
                std::to_string(vinfo.field_width) + "-sample row away from the start "
                "of the digital active line");
        }
        if (*sample < 0) *sample += vinfo.field_width;
    }

    const int firstActiveRowSample = src->standardSampleToRowSample(firstActiveSample);
    const int lastActiveRowSample = src->standardSampleToRowSample(lastActiveSample);
    firstActiveFrameLine = src->signalLineToFrameLine(firstActiveLine);
    const int lastActiveFrameLine = src->signalLineToFrameLine(lastActiveLine);

    // Both bounds convert individually, so all that is left to check is that
    // they come out the right way round in the source's raster. Horizontally
    // that means the window doesn't run off the end of a row; vertically, that
    // the first bound really is the topmost line.
    if (firstActiveRowSample > lastActiveRowSample) {
        throw VSAnalogException(
            "Active sample range " + std::to_string(firstActiveSample) + ".." +
            std::to_string(lastActiveSample) + " runs off the end of the source's rows: "
            "in the source's own alignment they are samples " +
            std::to_string(firstActiveRowSample) + " and " +
            std::to_string(lastActiveRowSample) + " of a " +
            std::to_string(vinfo.field_width) + "-sample row");
    }
    if (firstActiveFrameLine > lastActiveFrameLine) {
        throw VSAnalogException(
            "Active line " + std::to_string(firstActiveLine) + " sits below line " +
            std::to_string(lastActiveLine) + " in the woven frame, so they are not a "
            "top-to-bottom range. The first bound names the window's topmost line, "
            "and the two fields' line numbers interleave rather than run down the "
            "raster (this system's standard window is " + std::to_string(win.firstLine) +
            ".." + std::to_string(win.lastLine) + ")");
    }

    // Resolve the output color family, and from it the libchromadec output
    // format string plus the VapourSynth clip format.
    std::string cf = opts->colorFamily;
    if (cf.empty()) {
        cf = (opts->decoder == "mono") ? "gray" : "yuv";
    }
    if (isSecam && cf == "rgb") {
        throw VSAnalogException("RGB output is not supported for SECAM");
    }

    const char *outputFormat;
    if (cf == "gray") {
        outputFormat = "grays";
        vsFormat = VSFormat::Gray;
    } else if (cf == "rgb") {
        outputFormat = "rgbs";
        vsFormat = VSFormat::RGB;
    } else {  // yuv
        if (isSecam) {
            outputFormat = "yuv440ps";
            vsFormat = VSFormat::YUV440;
        } else {
            outputFormat = "yuv444ps";
            vsFormat = VSFormat::YUV444;
        }
    }

    // The 4:4:0 chroma planes weave both fields' half-rate lattices, which
    // tiles only over whole two-line pairs of each field, so libchromadec takes
    // the active line count in multiples of 4. The standard window is 576
    // lines; only an explicit crop hits this.
    if (vsFormat == VSFormat::YUV440 &&
        (lastActiveFrameLine - firstActiveFrameLine + 1) % 4 != 0) {
        throw VSAnalogException(
            "SECAM 4:4:0 output needs an active line count divisible by 4, but "
            "lines " + std::to_string(firstActiveLine) + ".." +
            std::to_string(lastActiveLine) + " span " +
            std::to_string(lastActiveFrameLine - firstActiveFrameLine + 1));
    }

    // Resolve the decoder kind.
    chd_decoder_kind_t kind = parseDecoder(opts->decoder);
    if (kind == CHD_DEC_AUTO && isSecam) {
        kind = CHD_DEC_SECAM;
    }
    if (isNnDecoder(kind)) {
        if (opts->modelPath.empty()) {
            throw VSAnalogException(
                "Neural-network decoder selected but no model path was supplied "
                "(provide model_version or model_path)");
        }
        if (vinfo.standard != CHD_STD_NTSC) {
            throw VSAnalogException("Neural-network decoders are NTSC-only");
        }
    }

    src->createDecoder(kind);

    // Decode knobs (tolerant setters silently skip options not meaningful for
    // the chosen decoder, e.g. phase_compensation on a PAL decoder).
    src->setOptF64(CHD_OPT_CHROMA_GAIN, opts->chromaGain);
    src->setOptF64(CHD_OPT_CHROMA_PHASE_DEG, opts->chromaPhase);
    src->setOptF64(CHD_OPT_CHROMA_NR_LEVEL, opts->chromaNR);
    src->setOptF64(CHD_OPT_LUMA_NR_LEVEL, opts->lumaNR);
    // libchromadec defaults to no padding, which is what we want: the output is
    // exactly the active window, and callers add borders with std.AddBorders.
    src->setOptI32Required(CHD_OPT_FIRST_ACTIVE_SAMPLE, firstActiveRowSample);
    src->setOptI32Required(CHD_OPT_LAST_ACTIVE_SAMPLE, lastActiveRowSample);
    src->setOptI32Required(CHD_OPT_FIRST_ACTIVE_FRAME_LINE, firstActiveFrameLine);
    src->setOptI32Required(CHD_OPT_LAST_ACTIVE_FRAME_LINE, lastActiveFrameLine);
    // Let the decoder's internal per-frame pool auto-size; VapourSynth pulls
    // one frame at a time (fmUnordered) and each decode parallelises here.
    src->setOptI32(CHD_OPT_THREAD_COUNT, 0);
    src->setOptBool(CHD_OPT_REVERSE_FIELD_ORDER, opts->reverseFields);
    src->setOptBool(CHD_OPT_PHASE_COMPENSATION, opts->phaseCompensation);
    if (!opts->colorDiffPrecision.empty()) {
        src->setOptStr(CHD_OPT_COLOR_DIFFERENCE_PRECISION,
                       opts->colorDiffPrecision.c_str());
        // commit() rejects anything but these two spellings, so an exact match
        // is the whole of the classic case.
        classicColorDifference = (opts->colorDiffPrecision == "classic");
    }
    if (!opts->broadcastScalingPrecision.empty()) {
        src->setOptStr(CHD_OPT_BROADCAST_SCALING_PRECISION,
                       opts->broadcastScalingPrecision.c_str());
    }
    src->setOptStrRequired(CHD_OPT_OUTPUT_FORMAT, outputFormat);

    if (isNnDecoder(kind)) {
        src->setOptF64(CHD_OPT_NN_INPUT_MAGNITUDE_SCALE, opts->modelInputScale);
        src->setOptBool(CHD_OPT_NN_CHROMA_BANDPASS, opts->modelChromaBandpass);
    }

    dropoutCorrect = opts->dropoutCorrect;
    dropoutOvercorrect = opts->dropoutOvercorrect;
    annotateDropouts = opts->annotateDropouts;
    if (dropoutCorrect) {
        src->setDropout(true, opts->dropoutOvercorrect, opts->dropoutIntra);
    }

    if (isNnDecoder(kind)) {
        src->setNnModel(opts->modelPath, providerToBackend(opts->onnxProvider),
                        parsePrecision(opts->modelPrecision));
    }

    src->commit();

    const chd_output_info_t &oinfo = src->outputInfo();
    width = oinfo.width;
    height = oinfo.height;
    numFrames = oinfo.num_frames;

    // Unpadded, the output is exactly the active window. Frame copies assume
    // that, so fail loudly rather than overrun a plane if it ever stops holding.
    const int activeWidth = lastActiveRowSample - firstActiveRowSample + 1;
    const int activeHeight = lastActiveFrameLine - firstActiveFrameLine + 1;
    if (width != activeWidth || height != activeHeight) {
        throw VSAnalogException(
            "Decoder returned " + std::to_string(width) + "x" + std::to_string(height) +
            " for a " + std::to_string(activeWidth) + "x" + std::to_string(activeHeight) +
            " active window");
    }

    // Constant frame rate from the video system.
    if (isNtscChromaticity) {
        fps = {30000, 1001};  // NTSC / PAL-M
    } else {
        fps = {25, 1};        // PAL / SECAM
    }
}

VSAnalog4fscSource::SampleAspectRatio VSAnalog4fscSource::GetSAR() const {
    // Follows ld-chroma-decoder's Y4M output (EBU R92 / SMPTE RP 187, scaled
    // from BT.601 13.5 MHz to 4𝑓𝑠𝑐). SECAM shares PAL's 625-line ratios.
    if (isNtscChromaticity) {
        return isWidescreen ? SampleAspectRatio{25, 22}
                            : SampleAspectRatio{352, 413};
    }
    return isWidescreen ? SampleAspectRatio{865, 779}
                        : SampleAspectRatio{259, 311};
}

void VSAnalog4fscSource::GetFrame(int frameNumber, float *const *planeData,
                                  const int *planeStride, int numPlanes,
                                  FrameExtra &extra) {
    std::lock_guard<std::mutex> lock(decodeMutex);

    chd_frame_t *f = src->decodeFrame(frameNumber);

    try {
        if (dropoutCorrect) {
            chd_dropout_stats_t st;
            if (src->lastDropoutStats(st)) {
                extra.hasDropoutStats = true;
                extra.dropoutCorrected = st.corrected;
                extra.dropoutFailed = st.failed;
                extra.dropoutTotalDistance = st.total_distance;
            }
        }

        // After the decode, so the SECAM click concealment this frame performed
        // is reported alongside the dropouts flagged in the source metadata.
        if (annotateDropouts) {
            src->dropoutSpans(frameNumber, dropoutOvercorrect, extra.dropoutSpans);
            extra.hasDropoutSpans = true;
        }

        if (isSecam) {
            chd_chroma_ident_report_t rep;
            if (chd_frame_get_chroma_ident(f, &rep) == CHD_OK) {
                extra.hasSecamComponent = true;
                extra.secamFirstRowComponent =
                    (rep.first_row_component == CHD_CHROMA_ROW_DR) ? 1 : 0;
            }
        }

        // Plane mapping per VS format. RGB: VS plane 0/1/2 = R/G/B.
        chd_plane_t planeMap[3];
        int mapCount;
        switch (vsFormat) {
            case VSFormat::Gray:
                planeMap[0] = CHD_PLANE_Y;
                mapCount = 1;
                break;
            case VSFormat::RGB:
                planeMap[0] = CHD_PLANE_R;
                planeMap[1] = CHD_PLANE_G;
                planeMap[2] = CHD_PLANE_B;
                mapCount = 3;
                break;
            default:  // YUV444 / YUV440
                planeMap[0] = CHD_PLANE_Y;
                planeMap[1] = CHD_PLANE_CB;
                planeMap[2] = CHD_PLANE_CR;
                mapCount = 3;
                break;
        }

        const int planes = numPlanes < mapCount ? numPlanes : mapCount;
        const size_t rowBytes = static_cast<size_t>(width) * sizeof(float);

        for (int i = 0; i < planes; i++) {
            const chd_plane_t p = planeMap[i];
            const float *srcData = nullptr;
            ptrdiff_t srcStride = 0;
            chd_status_t s = chd_frame_get_plane_float(f, p, &srcData, &srcStride);
            if (s != CHD_OK) {
                throw VSAnalogException(std::string("failed to read plane: ") +
                                        chd_status_str(s));
            }

            auto *dst = reinterpret_cast<uint8_t *>(planeData[i]);
            const auto dstStride = static_cast<ptrdiff_t>(planeStride[i]);
            const auto *srcBytes = reinterpret_cast<const uint8_t *>(srcData);

            const bool subsampledChroma =
                (vsFormat == VSFormat::YUV440) &&
                (p == CHD_PLANE_CB || p == CHD_PLANE_CR);

            if (subsampledChroma) {
                // Line-sequential 4:4:0 chroma. libchromadec weaves each plane
                // by output-row parity, which is the interleave the VS chroma
                // plane wants, and the plane spans the whole half-height, so it
                // copies row for row. first_frame_row names the frame row plane
                // row 0 was decoded from, not the plane's topmost line, so it
                // is not an offset to place the plane at.
                chd_plane_info_t pi;
                if (chd_frame_get_plane_info(f, p, &pi) != CHD_OK) {
                    throw VSAnalogException("failed to read chroma plane info");
                }
                const int vsChromaHeight = height / 2;
                if (pi.height != vsChromaHeight) {
                    throw VSAnalogException(
                        "4:4:0 chroma plane has " + std::to_string(pi.height) +
                        " rows, expected " + std::to_string(vsChromaHeight));
                }
                const int copyW = pi.width < width ? pi.width : width;
                const size_t copyBytes = static_cast<size_t>(copyW) * sizeof(float);
                for (int r = 0; r < vsChromaHeight; r++) {
                    auto *row = dst + r * dstStride;
                    std::memcpy(row, srcBytes + r * srcStride, copyBytes);
                    if (copyBytes < rowBytes) {
                        std::memset(row + copyBytes, 0, rowBytes - copyBytes);
                    }
                }
            } else {
                for (int y = 0; y < height; y++) {
                    std::memcpy(dst + y * dstStride, srcBytes + y * srcStride,
                                rowBytes);
                }
            }
        }
    } catch (...) {
        chd_frame_free(f);
        throw;
    }

    chd_frame_free(f);
}
