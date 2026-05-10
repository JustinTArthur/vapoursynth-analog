/******************************************************************************
 * ldzeug_decoders.cpp
 * vapoursynth-analog - NN-based NTSC decoders derived from jsaowji/ldzeug2
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 ******************************************************************************/

#include "ldzeug_decoders.h"

#include <QFile>
#include <array>
#include <cmath>
#include <stdexcept>
#include <vector>

namespace {

// One Ort env shared across all ldzeug decoder instances. ORT prints a
// warning if you create more than one Env in a process, and the env carries
// the threadpool — sharing it keeps total thread allocation predictable.
Ort::Env &sharedOrtEnv() {
    static Ort::Env env(ORT_LOGGING_LEVEL_WARNING, "vsanalog-ldzeug");
    return env;
}

Ort::SessionOptions makeSessionOptions() {
    Ort::SessionOptions opts;
    opts.SetIntraOpNumThreads(1);
    opts.SetInterOpNumThreads(1);
    opts.SetGraphOptimizationLevel(GraphOptimizationLevel::ORT_ENABLE_ALL);
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

}  // namespace

// =====================================================================
// LdzeugDecoderBase
// =====================================================================

void LdzeugDecoderBase::configure(
    const LdDecodeMetaData::VideoParameters &vp, const QString &modelPath) {
    videoParameters = vp;
    if (!ortEnv) {
        // We don't actually own the env (it's process-shared), but holding
        // a reference here keeps the Session API happy without a global
        // reach-around in derived code.
    }
    ortSession = openSession(modelPath, makeSessionOptions());
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

    // chromaPhase rotation constants (lifted out of the per-field loop).
    const double theta = (IQ_BASE_ANGLE_DEG + chromaPhase) * M_PI / 180.0;
    const double bp = std::sin(theta) * chromaGain;
    const double bq = std::cos(theta) * chromaGain;

    // Reused per field: a 3-channel input tensor and a 3-channel output.
    std::vector<float> inputBuffer(3 * fieldStride);
    Ort::MemoryInfo memInfo = Ort::MemoryInfo::CreateCpu(
        OrtArenaAllocator, OrtMemTypeDefault);

    const std::array<int64_t, 4> inputShape = {
        1, 3, fieldHeight, fieldWidth};

    Ort::AllocatedStringPtr inputName = ortSession->GetInputNameAllocated(0, ortAllocator);
    Ort::AllocatedStringPtr outputName = ortSession->GetOutputNameAllocated(0, ortAllocator);
    const char *inputNamePtr = inputName.get();
    const char *outputNamePtr = outputName.get();

    for (qint32 frameIdx = startIndex; frameIdx + 1 < endIndex && frameIdx + 1 < inputFields.size();
         frameIdx += 2) {
        const qint32 outFrame = (frameIdx - startIndex) / 2;
        if (outFrame >= componentFrames.size()) break;

        ComponentFrame &out = componentFrames[outFrame];

        // Run inference per field, then weave Y/U/V into the interlaced frame.
        for (qint32 sub = 0; sub < 2; ++sub) {
            const SourceField &field = inputFields[frameIdx + sub];
            const qint32 fieldPhase = field.field.fieldPhaseID;
            const qint32 yOffset = field.getOffset();  // 0 = top field

            float *cvbsPlane = inputBuffer.data() + 0 * fieldStride;
            float *iCarPlane = inputBuffer.data() + 1 * fieldStride;
            float *qCarPlane = inputBuffer.data() + 2 * fieldStride;

            writeFieldCvbsToFloatPlane(field, vp, cvbsPlane);
            writeICarrierPlane(fieldPhase, vp, iCarPlane);
            writeQCarrierPlane(fieldPhase, vp, qCarPlane);

            Ort::Value inputTensor = Ort::Value::CreateTensor<float>(
                memInfo, inputBuffer.data(), inputBuffer.size(),
                inputShape.data(), inputShape.size());

            auto outputs = ortSession->Run(
                Ort::RunOptions{nullptr},
                &inputNamePtr, &inputTensor, 1,
                &outputNamePtr, 1);

            if (outputs.empty()) {
                throw std::runtime_error(
                    "ldzeug2_color_cnn inference produced no outputs");
            }

            const float *outData = outputs[0].GetTensorData<float>();
            const auto outShape = outputs[0].GetTensorTypeAndShapeInfo().GetShape();
            if (outShape.size() != 4 || outShape[1] != 3) {
                throw std::runtime_error(
                    "ldzeug2_color_cnn: unexpected output tensor shape");
            }

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

    std::vector<float> fieldBuffer(fieldStride);
    std::vector<float> frameBuffer;
    if (mode == Mode::Frame) frameBuffer.resize(frameStride);

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

        if (mode == Mode::Frame) {
            // Frame model: weave both fields into a single interlaced frame
            // input, then run one inference. Frame layout: top field on
            // even rows, bottom field on odd rows.
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
            for (qint32 y = 0; y < frameHeight; ++y) {
                double *yRow = out.y(y);
                double *uRow = out.u(y);
                double *vRow = out.v(y);
                for (qint32 x = 0; x < fieldWidth; ++x) {
                    yRow[x] = static_cast<double>(yOut[y * fieldWidth + x]) * FROM_NORMALIZED;
                    uRow[x] = 0.0;
                    vRow[x] = 0.0;
                }
            }
        } else {
            // Field model: one inference per field, weave outputs.
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
                for (qint32 y = 0; y < fieldHeight; ++y) {
                    const qint32 frameLine = y * 2 + yOffset;
                    if (frameLine >= frameHeight) break;
                    double *yRow = out.y(frameLine);
                    double *uRow = out.u(frameLine);
                    double *vRow = out.v(frameLine);
                    for (qint32 x = 0; x < fieldWidth; ++x) {
                        yRow[x] = static_cast<double>(yOut[y * fieldWidth + x]) * FROM_NORMALIZED;
                        uRow[x] = 0.0;
                        vRow[x] = 0.0;
                    }
                }
            }
        }
    }
}

// =====================================================================
// Helper: model-version → Mode
// =====================================================================

LdzeugLumaSepDecoder::Mode lumaSepModeFromModelPath(const QString &path) {
    // The bundled filenames carry the variant in the last basename token:
    //   ldzeug2_luma_sep_field.onnx → Mode::Field
    //   ldzeug2_luma_sep_frame.onnx → Mode::Frame
    // Custom paths fall back to Field which matches the python-side default.
    const QString lower = path.toLower();
    if (lower.contains(QStringLiteral("frame"))) {
        return LdzeugLumaSepDecoder::Mode::Frame;
    }
    return LdzeugLumaSepDecoder::Mode::Field;
}