/******************************************************************************
 * ldzeug_decoders.cpp
 * vapoursynth-analog - NN-based NTSC decoders derived from jsaowji/ldzeug2
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 ******************************************************************************/

#include "ldzeug_decoders.h"

#include <QDebug>
#include <QFile>
#include <array>
#include <cmath>
#include <stdexcept>
#include <vector>

// Reuse tbc-tools' filter constants and FIR template directly. Both headers
// are header-only and on our include path via library/filter.
#include "deemp.h"
#include "firfilter.h"

namespace {

// One Ort env shared across all ldzeug decoder instances. ORT prints a
// warning if you create more than one Env in a process, and the env carries
// the threadpool — sharing it keeps total thread allocation predictable.
Ort::Env &sharedOrtEnv() {
    static Ort::Env env(ORT_LOGGING_LEVEL_WARNING, "vsanalog-ldzeug");
    return env;
}

Ort::SessionOptions makeSessionOptions(const QString &requestedProvider) {
    Ort::SessionOptions opts;
    opts.SetIntraOpNumThreads(1);
    opts.SetInterOpNumThreads(1);
    opts.SetGraphOptimizationLevel(GraphOptimizationLevel::ORT_ENABLE_ALL);

    const QString pref = requestedProvider.trimmed().toLower();
    const bool wantsCpuOnly = (pref == "cpu");
    auto tryEP = [&opts](const char *name,
                         const std::unordered_map<std::string, std::string> &epOpts = {}) {
        try {
            opts.AppendExecutionProvider(name, epOpts);
            qInfo() << "ldzeug decoder: requested" << name << "execution provider";
        } catch (const std::exception &e) {
            qWarning() << "ldzeug decoder:" << name
                       << "EP unavailable; falling back to CPU:" << e.what();
        }
    };

    if (wantsCpuOnly) {
        return opts;  // skip every accelerator attempt
    }

#if defined(__APPLE__)
    // macOS default: CoreML. Honor an explicit "coreml" too.
    if (pref.isEmpty() || pref == "auto" || pref == "coreml") {
        tryEP("CoreML", {{"MLComputeUnits", "ALL"}, {"ModelFormat", "MLProgram"}});
    }
#endif
    // Cross-platform GPU EPs by request. CPU is always the implicit fallback
    // for ops the chosen EP can't handle.
    if (pref == "cuda" || pref == "gpu") tryEP("CUDA");
    else if (pref == "migraphx") tryEP("MIGraphX");
    else if (pref == "tensorrt" || pref == "trt") tryEP("Tensorrt");

    return opts;
}

// Build an Ort::Session from a filesystem path. Path is encoded so non-ASCII
// works on POSIX (QFile::encodeName) and Windows (UTF-16).
std::unique_ptr<Ort::Session> openSession(const QString &modelPath,
                                          const Ort::SessionOptions &opts) {
    if (modelPath.isEmpty()) {
        throw std::runtime_error(
            "ldzeug decoder requires a model path; none was supplied");
    }
#ifdef _WIN32
    const std::wstring path = modelPath.toStdWString();
    return std::make_unique<Ort::Session>(sharedOrtEnv(), path.c_str(), opts);
#else
    const QByteArray bytes = QFile::encodeName(modelPath);
    const std::string path(bytes.constData(), static_cast<size_t>(bytes.size()));
    return std::make_unique<Ort::Session>(sharedOrtEnv(), path.c_str(), opts);
#endif
}

// I-channel rotation angle for U/V derivation (matches ldzeug2's uv_from_iq
// when chromaPhase is in degrees and added to 33°).
constexpr double IQ_BASE_ANGLE_DEG = 33.0;

// fieldPhase {1,2,3,4} → carrier sign for color_cnn. Matches the table in
// ldzeug2.colordecoder.input_for_color_cnn.
double fieldPhaseSign(qint32 fieldPhase) {
    switch (fieldPhase) {
        case 1: return  1.0;
        case 2: return -1.0;
        case 3: return -1.0;
        case 4: return  1.0;
        default: return 1.0;  // unknown phase; preserve input
    }
}

// Flatten a single field's worth of CVBS samples into a NCHW float tensor
// row in [0,1] approximately, matching what the ldzeug2 inference path
// expects (it operates on TBC samples normalized to [0,1] via /65535).
//
// `dst` is a contiguous buffer of fieldHeight * fieldWidth floats; the
// caller is responsible for any channel-axis stride.
void writeFieldCvbsToFloatPlane(const SourceField &field,
                                const LdDecodeMetaData::VideoParameters &vp,
                                float *dst) {
    const qint32 width = vp.fieldWidth;
    const qint32 height = vp.fieldHeight;
    constexpr float scale = 1.0f / 65535.0f;
    const auto *src = field.data.data();
    for (qint32 y = 0; y < height; ++y) {
        for (qint32 x = 0; x < width; ++x) {
            dst[y * width + x] = static_cast<float>(src[y * width + x]) * scale;
        }
    }
}

// Synthesize the I-carrier (cosine) plane for a single field per
// ldzeug2.colordecoder.input_for_color_cnn:
//   cc = (oX / (4*fsc)) * 2*pi*fsc - pi/2
//   mod1 = cos(cc) * (oY%2 == 1 ? -1 : 1) * fieldPhaseSign(fieldPhase)
//
// Note that cos((oX*pi)/2 - pi/2) gives the integer-pixel sequence
// [0, 1, 0, -1, 0, 1, ...], matching the analytical NTSC I-axis pattern.
void writeICarrierPlane(qint32 fieldPhase,
                        const LdDecodeMetaData::VideoParameters &vp,
                        float *dst) {
    const qint32 width = vp.fieldWidth;
    const qint32 height = vp.fieldHeight;
    const double phaseSign = fieldPhaseSign(fieldPhase);
    // Per integer x: cos((x*pi)/2 - pi/2) → cycles through {0, 1, 0, -1}.
    static constexpr std::array<double, 4> baseI = {0.0, 1.0, 0.0, -1.0};
    for (qint32 y = 0; y < height; ++y) {
        const double rowSign = (y % 2 == 1 ? -1.0 : 1.0) * phaseSign;
        for (qint32 x = 0; x < width; ++x) {
            dst[y * width + x] = static_cast<float>(baseI[x % 4] * rowSign);
        }
    }
}

// Q-carrier (sine) plane. sin((x*pi)/2 - pi/2) → {-1, 0, 1, 0}.
void writeQCarrierPlane(qint32 fieldPhase,
                        const LdDecodeMetaData::VideoParameters &vp,
                        float *dst) {
    const qint32 width = vp.fieldWidth;
    const qint32 height = vp.fieldHeight;
    const double phaseSign = fieldPhaseSign(fieldPhase);
    static constexpr std::array<double, 4> baseQ = {-1.0, 0.0, 1.0, 0.0};
    for (qint32 y = 0; y < height; ++y) {
        const double rowSign = (y % 2 == 1 ? -1.0 : 1.0) * phaseSign;
        for (qint32 x = 0; x < width; ++x) {
            dst[y * width + x] = static_cast<float>(baseQ[x % 4] * rowSign);
        }
    }
}

// Map ldzeug2's [0,1] float Y/I/Q outputs into the 16bIre composite-signal
// scale our analog4fsc.cpp converter expects. ldzeug2 trains against TBC
// samples in [0,1] where 16bIre 0..65535 → 0..1 directly, so a simple ×65535
// is the right inverse.
constexpr double FROM_NORMALIZED = 65535.0;

// Apply ldzeug2's uv_from_iq transform (rotation by IQ_BASE_ANGLE_DEG +
// chromaPhase, scaled by chromaGain). Inputs and outputs are in the same
// scale; we operate in 16bIre-equivalent units after multiplying through.
//
//   theta = (33 + chromaPhase) * pi / 180
//   bp = sin(theta) * chromaGain
//   bq = cos(theta) * chromaGain
//   u = -bp*i + bq*q
//   v =  bq*i + bp*q
void applyUvFromIq(double i, double q, double bp, double bq,
                   double *u, double *v) {
    *u = -bp * i + bq * q;
    *v =  bq * i + bp * q;
}

// NTSC line phase: matches Comb::FrameBuffer::getLinePhase math. ``line``
// is the 0-based interlaced-frame line; even lines come from the first
// field, odd from the second.
bool getLinePhase(qint32 line, qint32 firstFieldPhaseID, qint32 secondFieldPhaseID) {
    const bool isFirstField = ((line % 2) == 0);
    const qint32 fieldID = isFirstField ? firstFieldPhaseID : secondFieldPhaseID;
    const bool isPositivePhaseOnEvenLines = (fieldID == 1) || (fieldID == 4);
    const qint32 fieldLine = line / 2;
    const bool isEvenLine = (fieldLine % 2) == 0;
    return isEvenLine ? isPositivePhaseOnEvenLines : !isPositivePhaseOnEvenLines;
}

// Holds (firstFieldPhaseID, secondFieldPhaseID) for a frame, where
// "first" means the field whose lines land on even rows (offset=0).
struct FramePhaseIDs {
    qint32 first;
    qint32 second;
};

FramePhaseIDs framePhaseIDs(const SourceField &fa, const SourceField &fb) {
    if (fa.getOffset() == 0) {
        return {fa.field.fieldPhaseID, fb.field.fieldPhaseID};
    }
    return {fb.field.fieldPhaseID, fa.field.fieldPhaseID};
}

// Demodulate I/Q from one row of CVBS minus the NN-supplied Y, then derive
// U/V via the uv_from_iq rotation. ``cvbsRow`` is the raw 16b composite row
// (one field/frame row, width = ``fieldWidth``). ``yNormRow`` is the NN's
// luma row in [0,1]. Y is written for every pixel; U/V are zero outside the
// horizontal active range. ``iWork``/``qWork``/``iFilt``/``qFilt`` are
// caller-owned scratch buffers, resized as needed.
void demodChromaRow(const quint16 *cvbsRow,
                    const float *yNormRow,
                    qint32 fieldWidth,
                    qint32 activeStart, qint32 activeEnd,
                    qint32 frameLine,
                    qint32 firstFieldPhaseID, qint32 secondFieldPhaseID,
                    bool chromaBandpass,
                    double bp, double bq,
                    std::vector<double> &iWork,
                    std::vector<double> &qWork,
                    std::vector<double> &iFilt,
                    std::vector<double> &qFilt,
                    double *yRow, double *uRow, double *vRow) {
    for (qint32 x = 0; x < fieldWidth; ++x) {
        yRow[x] = static_cast<double>(yNormRow[x]) * FROM_NORMALIZED;
        uRow[x] = 0.0;
        vRow[x] = 0.0;
    }
    if (activeStart >= activeEnd) return;

    const qint32 activeLen = activeEnd - activeStart;
    if (static_cast<qint32>(iWork.size()) < activeLen) {
        iWork.resize(activeLen);
        qWork.resize(activeLen);
        iFilt.resize(activeLen);
        qFilt.resize(activeLen);
    }

    const bool linePhase = getLinePhase(frameLine, firstFieldPhaseID, secondFieldPhaseID);

    // Quadrature switch mirroring Comb::FrameBuffer::splitIQ. si/sq carry
    // across the row so every pixel ends up with the most recent I and Q
    // samples; the FIR LP step (if enabled) smooths the resulting steps.
    double si = 0, sq = 0;
    for (qint32 h = activeStart; h < activeEnd; ++h) {
        const double C = static_cast<double>(cvbsRow[h]) - yRow[h];
        double cavg = linePhase ? -C : C;
        switch (h % 4) {
            case 0: sq =  cavg; break;
            case 1: si = -cavg; break;
            case 2: sq = -cavg; break;
            case 3: si =  cavg; break;
            default: break;
        }
        iWork[h - activeStart] = si;
        qWork[h - activeStart] = sq;
    }

    const double *iSrc = iWork.data();
    const double *qSrc = qWork.data();
    if (chromaBandpass) {
        auto iqFilter = makeFIRFilter(c_colorlp_b);
        iqFilter.apply(iWork.data(), iFilt.data(), activeLen);
        iqFilter.apply(qWork.data(), qFilt.data(), activeLen);
        iSrc = iFilt.data();
        qSrc = qFilt.data();
    }

    for (qint32 h = activeStart; h < activeEnd; ++h) {
        const qint32 idx = h - activeStart;
        applyUvFromIq(iSrc[idx], qSrc[idx], bp, bq, &uRow[h], &vRow[h]);
    }
}

}  // namespace

// =====================================================================
// LdzeugDecoderBase
// =====================================================================

void LdzeugDecoderBase::configure(
    const LdDecodeMetaData::VideoParameters &vp, const QString &modelPath,
    Mode inputMode, const QString &provider) {
    videoParameters = vp;
    mode = inputMode;
    ortSession = openSession(modelPath, makeSessionOptions(provider));
    qInfo() << "ldzeug decoder: input mode"
            << (mode == Mode::Frame ? "frame (weaved)" : "field");
}

// =====================================================================
// LdzeugColorCnnDecoder
// =====================================================================

void LdzeugColorCnnDecoder::decodeFrames(
    const QVector<SourceField> &inputFields,
    qint32 startIndex, qint32 endIndex,
    QVector<ComponentFrame> &componentFrames) {

    if (!ortSession) {
        throw std::runtime_error("ldzeug2_color_cnn decoder not configured");
    }

    const auto &vp = videoParameters;
    const qint32 fieldWidth = vp.fieldWidth;
    const qint32 fieldHeight = vp.fieldHeight;
    const qint32 frameHeight = fieldHeight * 2;
    const size_t fieldStride = static_cast<size_t>(fieldWidth) * fieldHeight;
    const size_t frameStride = static_cast<size_t>(fieldWidth) * frameHeight;

    // chromaPhase rotation constants (lifted out of the per-field loop).
    const double theta = (IQ_BASE_ANGLE_DEG + chromaPhase) * M_PI / 180.0;
    const double bp = std::sin(theta) * chromaGain;
    const double bq = std::cos(theta) * chromaGain;

    const size_t planeStride = (mode == Mode::Frame) ? frameStride : fieldStride;
    std::vector<float> inputBuffer(3 * planeStride);
    Ort::MemoryInfo memInfo = Ort::MemoryInfo::CreateCpu(
        OrtArenaAllocator, OrtMemTypeDefault);

    const qint32 modelHeight = (mode == Mode::Frame) ? frameHeight : fieldHeight;
    const std::array<int64_t, 4> inputShape = {1, 3, modelHeight, fieldWidth};

    Ort::AllocatedStringPtr inputName = ortSession->GetInputNameAllocated(0, ortAllocator);
    Ort::AllocatedStringPtr outputName = ortSession->GetOutputNameAllocated(0, ortAllocator);
    const char *inputNamePtr = inputName.get();
    const char *outputNamePtr = outputName.get();

    auto runOnce = [&](Ort::Value &&inputTensor) {
        auto outputs = ortSession->Run(
            Ort::RunOptions{nullptr},
            &inputNamePtr, &inputTensor, 1,
            &outputNamePtr, 1);
        if (outputs.empty()) {
            throw std::runtime_error(
                "ldzeug2_color_cnn inference produced no outputs");
        }
        const auto outShape = outputs[0].GetTensorTypeAndShapeInfo().GetShape();
        if (outShape.size() != 4 || outShape[1] != 3) {
            throw std::runtime_error(
                "ldzeug2_color_cnn: unexpected output tensor shape");
        }
        return outputs;
    };

    for (qint32 frameIdx = startIndex; frameIdx + 1 < endIndex && frameIdx + 1 < inputFields.size();
         frameIdx += 2) {
        const qint32 outFrame = (frameIdx - startIndex) / 2;
        if (outFrame >= componentFrames.size()) break;

        ComponentFrame &out = componentFrames[outFrame];

        if (mode == Mode::Frame) {
            // Weave both fields into a single 3-channel weaved-frame input.
            // For each output line in the interlaced frame, we know which
            // source field it came from (top: even, bottom: odd), so the
            // I/Q-carrier signs are computed per-output-line using that
            // source field's fieldPhase.
            float *cvbsPlane = inputBuffer.data() + 0 * frameStride;
            float *iCarPlane = inputBuffer.data() + 1 * frameStride;
            float *qCarPlane = inputBuffer.data() + 2 * frameStride;

            constexpr float scale = 1.0f / 65535.0f;
            static constexpr std::array<double, 4> baseI = {0.0, 1.0, 0.0, -1.0};
            static constexpr std::array<double, 4> baseQ = {-1.0, 0.0, 1.0, 0.0};

            for (qint32 sub = 0; sub < 2; ++sub) {
                const SourceField &field = inputFields[frameIdx + sub];
                const qint32 yOffset = field.getOffset();
                const qint32 fieldPhase = field.field.fieldPhaseID;
                const double phaseSign = fieldPhaseSign(fieldPhase);
                const auto *src = field.data.data();
                for (qint32 y = 0; y < fieldHeight; ++y) {
                    const qint32 frameLine = y * 2 + yOffset;
                    if (frameLine >= frameHeight) break;
                    // Within the weaved frame, the original field-local
                    // "y%2" parity is encoded by the field that contributed
                    // the line (sub=0/top -> phaseFactor based on y%2 of
                    // the source field).
                    const double rowSign = (y % 2 == 1 ? -1.0 : 1.0) * phaseSign;
                    float *cv = cvbsPlane + frameLine * fieldWidth;
                    float *ic = iCarPlane + frameLine * fieldWidth;
                    float *qc = qCarPlane + frameLine * fieldWidth;
                    for (qint32 x = 0; x < fieldWidth; ++x) {
                        cv[x] = static_cast<float>(src[y * fieldWidth + x]) * scale;
                        ic[x] = static_cast<float>(baseI[x % 4] * rowSign);
                        qc[x] = static_cast<float>(baseQ[x % 4] * rowSign);
                    }
                }
            }

            Ort::Value inputTensor = Ort::Value::CreateTensor<float>(
                memInfo, inputBuffer.data(), inputBuffer.size(),
                inputShape.data(), inputShape.size());
            auto outputs = runOnce(std::move(inputTensor));

            const float *outData = outputs[0].GetTensorData<float>();
            const float *yPlane = outData + 0 * frameStride;
            const float *iPlane = outData + 1 * frameStride;
            const float *qPlane = outData + 2 * frameStride;

            for (qint32 fy = 0; fy < frameHeight; ++fy) {
                double *yRow = out.y(fy);
                double *uRow = out.u(fy);
                double *vRow = out.v(fy);
                for (qint32 x = 0; x < fieldWidth; ++x) {
                    const double iVal = static_cast<double>(iPlane[fy * fieldWidth + x]) * FROM_NORMALIZED;
                    const double qVal = static_cast<double>(qPlane[fy * fieldWidth + x]) * FROM_NORMALIZED;
                    double u, v;
                    applyUvFromIq(iVal, qVal, bp, bq, &u, &v);
                    yRow[x] = static_cast<double>(yPlane[fy * fieldWidth + x]) * FROM_NORMALIZED;
                    uRow[x] = u;
                    vRow[x] = v;
                }
            }
            continue;
        }

        // Field mode: per-field inference, then weave outputs.
        for (qint32 sub = 0; sub < 2; ++sub) {
            const SourceField &field = inputFields[frameIdx + sub];
            const qint32 fieldPhase = field.field.fieldPhaseID;
            const qint32 yOffset = field.getOffset();

            float *cvbsPlane = inputBuffer.data() + 0 * fieldStride;
            float *iCarPlane = inputBuffer.data() + 1 * fieldStride;
            float *qCarPlane = inputBuffer.data() + 2 * fieldStride;

            writeFieldCvbsToFloatPlane(field, vp, cvbsPlane);
            writeICarrierPlane(fieldPhase, vp, iCarPlane);
            writeQCarrierPlane(fieldPhase, vp, qCarPlane);

            Ort::Value inputTensor = Ort::Value::CreateTensor<float>(
                memInfo, inputBuffer.data(), inputBuffer.size(),
                inputShape.data(), inputShape.size());
            auto outputs = runOnce(std::move(inputTensor));

            const float *outData = outputs[0].GetTensorData<float>();
            const float *yPlane = outData + 0 * fieldStride;
            const float *iPlane = outData + 1 * fieldStride;
            const float *qPlane = outData + 2 * fieldStride;

            for (qint32 y = 0; y < fieldHeight; ++y) {
                const qint32 frameLine = y * 2 + yOffset;
                if (frameLine >= frameHeight) break;
                double *yRow = out.y(frameLine);
                double *uRow = out.u(frameLine);
                double *vRow = out.v(frameLine);
                for (qint32 x = 0; x < fieldWidth; ++x) {
                    const double iVal = static_cast<double>(iPlane[y * fieldWidth + x]) * FROM_NORMALIZED;
                    const double qVal = static_cast<double>(qPlane[y * fieldWidth + x]) * FROM_NORMALIZED;
                    double u, v;
                    applyUvFromIq(iVal, qVal, bp, bq, &u, &v);
                    yRow[x] = static_cast<double>(yPlane[y * fieldWidth + x]) * FROM_NORMALIZED;
                    uRow[x] = u;
                    vRow[x] = v;
                }
            }
        }
    }
}

// =====================================================================
// LdzeugLumaSepDecoder
// =====================================================================

void LdzeugLumaSepDecoder::decodeFrames(
    const QVector<SourceField> &inputFields,
    qint32 startIndex, qint32 endIndex,
    QVector<ComponentFrame> &componentFrames) {

    if (!ortSession) {
        throw std::runtime_error("ldzeug2_luma_sep decoder not configured");
    }

    const auto &vp = videoParameters;
    const qint32 fieldWidth = vp.fieldWidth;
    const qint32 fieldHeight = vp.fieldHeight;
    const qint32 frameHeight = fieldHeight * 2;
    const size_t fieldStride = static_cast<size_t>(fieldWidth) * fieldHeight;
    const size_t frameStride = fieldStride * 2;

    // Active region for chroma demod. Outside this range chroma stays 0.
    const qint32 activeStart = vp.activeVideoStart;
    const qint32 activeEnd = vp.activeVideoEnd;
    const qint32 firstActiveLine = vp.firstActiveFrameLine;
    const qint32 lastActiveLine = vp.lastActiveFrameLine;

    // chromaPhase rotation constants for uv_from_iq, lifted out of the loop.
    const double theta = (IQ_BASE_ANGLE_DEG + chromaPhase) * M_PI / 180.0;
    const double bp = std::sin(theta) * chromaGain;
    const double bq = std::cos(theta) * chromaGain;

    std::vector<float> fieldBuffer(fieldStride);
    std::vector<float> frameBuffer;
    if (mode == Mode::Frame) frameBuffer.resize(frameStride);

    // Scratch buffers for per-row I/Q demod and FIR LP output. Sized on
    // first use inside demodChromaRow.
    std::vector<double> iWork, qWork, iFilt, qFilt;

    Ort::MemoryInfo memInfo = Ort::MemoryInfo::CreateCpu(
        OrtArenaAllocator, OrtMemTypeDefault);

    Ort::AllocatedStringPtr inputName = ortSession->GetInputNameAllocated(0, ortAllocator);
    Ort::AllocatedStringPtr outputName = ortSession->GetOutputNameAllocated(0, ortAllocator);
    const char *inputNamePtr = inputName.get();
    const char *outputNamePtr = outputName.get();

    for (qint32 frameIdx = startIndex; frameIdx + 1 < endIndex && frameIdx + 1 < inputFields.size();
         frameIdx += 2) {
        const qint32 outFrame = (frameIdx - startIndex) / 2;
        if (outFrame >= componentFrames.size()) break;
        ComponentFrame &out = componentFrames[outFrame];

        // Resolve which field's phase ID goes with even vs. odd frame lines.
        const auto phaseIDs = framePhaseIDs(inputFields[frameIdx], inputFields[frameIdx + 1]);

        // Helper: demodulate U/V for frame line ``frameLine`` from the
        // already-written Y row and the raw CVBS row.
        auto demodLine = [&](qint32 frameLine,
                             const quint16 *cvbsRow,
                             const float *yNormRow) {
            const bool inActive = frameLine >= firstActiveLine
                                  && frameLine < lastActiveLine;
            const qint32 hStart = inActive ? activeStart : 0;
            const qint32 hEnd = inActive ? activeEnd : 0;
            demodChromaRow(cvbsRow, yNormRow, fieldWidth, hStart, hEnd,
                           frameLine, phaseIDs.first, phaseIDs.second,
                           chromaBandpass, bp, bq,
                           iWork, qWork, iFilt, qFilt,
                           out.y(frameLine), out.u(frameLine), out.v(frameLine));
        };

        if (mode == Mode::Frame) {
            // Frame model: weave both fields into a single interlaced frame
            // input, then run one inference.
            for (qint32 sub = 0; sub < 2; ++sub) {
                const SourceField &field = inputFields[frameIdx + sub];
                const qint32 yOffset = field.getOffset();
                const auto *src = field.data.data();
                constexpr float scale = 1.0f / 65535.0f;
                for (qint32 y = 0; y < fieldHeight; ++y) {
                    const qint32 frameLine = y * 2 + yOffset;
                    if (frameLine >= frameHeight) break;
                    float *dst = frameBuffer.data() + frameLine * fieldWidth;
                    for (qint32 x = 0; x < fieldWidth; ++x) {
                        dst[x] = static_cast<float>(src[y * fieldWidth + x]) * scale;
                    }
                }
            }
            const std::array<int64_t, 4> inputShape = {1, 1, frameHeight, fieldWidth};
            Ort::Value inputTensor = Ort::Value::CreateTensor<float>(
                memInfo, frameBuffer.data(), frameBuffer.size(),
                inputShape.data(), inputShape.size());
            auto outputs = ortSession->Run(
                Ort::RunOptions{nullptr},
                &inputNamePtr, &inputTensor, 1,
                &outputNamePtr, 1);
            if (outputs.empty()) {
                throw std::runtime_error(
                    "ldzeug2_luma_sep inference produced no outputs");
            }
            const float *yOut = outputs[0].GetTensorData<float>();
            // CVBS rows come from each source field's data, indexed by frame line.
            for (qint32 sub = 0; sub < 2; ++sub) {
                const SourceField &field = inputFields[frameIdx + sub];
                const qint32 yOffset = field.getOffset();
                const auto *src = field.data.data();
                for (qint32 y = 0; y < fieldHeight; ++y) {
                    const qint32 frameLine = y * 2 + yOffset;
                    if (frameLine >= frameHeight) break;
                    demodLine(frameLine,
                              src + y * fieldWidth,
                              yOut + frameLine * fieldWidth);
                }
            }
        } else {
            // Field model: one inference per field, demod inline per frame line.
            for (qint32 sub = 0; sub < 2; ++sub) {
                const SourceField &field = inputFields[frameIdx + sub];
                const qint32 yOffset = field.getOffset();
                writeFieldCvbsToFloatPlane(field, vp, fieldBuffer.data());

                const std::array<int64_t, 4> inputShape = {1, 1, fieldHeight, fieldWidth};
                Ort::Value inputTensor = Ort::Value::CreateTensor<float>(
                    memInfo, fieldBuffer.data(), fieldBuffer.size(),
                    inputShape.data(), inputShape.size());
                auto outputs = ortSession->Run(
                    Ort::RunOptions{nullptr},
                    &inputNamePtr, &inputTensor, 1,
                    &outputNamePtr, 1);
                if (outputs.empty()) {
                    throw std::runtime_error(
                        "ldzeug2_luma_sep inference produced no outputs");
                }
                const float *yOut = outputs[0].GetTensorData<float>();
                const auto *src = field.data.data();
                for (qint32 y = 0; y < fieldHeight; ++y) {
                    const qint32 frameLine = y * 2 + yOffset;
                    if (frameLine >= frameHeight) break;
                    demodLine(frameLine,
                              src + y * fieldWidth,
                              yOut + y * fieldWidth);
                }
            }
        }
    }
}
