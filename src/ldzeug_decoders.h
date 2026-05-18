/******************************************************************************
 * ldzeug_decoders.h
 * vapoursynth-analog - NN-based NTSC decoders derived from jsaowji/ldzeug2
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * This module supplies two neural-network-based NTSC decoders that bypass the
 * tbc-tools Comb pipeline entirely:
 *
 *   - LdzeugColorCnnDecoder: takes [CVBS, I-carrier, Q-carrier] (carriers
 *     analytically synthesized from sample position + fieldPhase) and emits
 *     [Y, I, Q] in one inference pass, replacing both Y/C separation and
 *     chroma demodulation. Per-field and weaved-frame are different input
 *     pipelines; the caller selects which by passing Mode to configure().
 *
 *   - LdzeugLumaSepDecoder: takes a CVBS field or frame and emits Y, then
 *     derives chroma as ``CVBS - Y`` and runs an analytical NTSC I/Q
 *     demodulator (mirroring jsaowji's comb_split_already) with the
 *     ``c_colorlp_b`` bandpass filter reused from tbc-tools' filter library.
 *     Mode is set explicitly by the caller.
 *
 * Both decoders share an Ort::Env to keep total ONNX Runtime allocation
 * proportional to the live decoder count rather than per-instance.
 ******************************************************************************/

#ifndef LDZEUG_DECODERS_H
#define LDZEUG_DECODERS_H

#include <QString>
#include <QVector>
#include <memory>
#include <onnxruntime_cxx_api.h>

#include "lddecodemetadata.h"
#include "componentframe.h"
#include "sourcefield.h"

// Common base: holds the shared Ort env, session options, per-instance
// session, and the field-vs-frame mode auto-detected from the model's
// input tensor shape at session-open time.
class LdzeugDecoderBase {
public:
    enum class Mode { Field, Frame };

    virtual ~LdzeugDecoderBase() = default;

    // Update video parameters and (re)load the ONNX session from modelPath.
    // ``provider`` is the EP name to request (e.g. "cuda", "rocm", "coreml");
    // empty means platform default. ``inputMode`` selects per-field vs.
    // weaved-frame input shaping at inference time -- it's a structural
    // pipeline choice, not a model-shape inspection, since jsaowji's bundled
    // weights advertise dynamic shapes.
    // Throws std::runtime_error on session load failure.
    void configure(const LdDecodeMetaData::VideoParameters &videoParameters,
                   const QString &modelPath,
                   Mode inputMode,
                   const QString &provider = {});

    // Process a sequence of fields into a sequence of full-frame ComponentFrames.
    // Mirrors Comb::decodeFrames signature so callers can dispatch uniformly.
    virtual void decodeFrames(const QVector<SourceField> &inputFields,
                              qint32 startIndex, qint32 endIndex,
                              QVector<ComponentFrame> &componentFrames) = 0;

    // Look-behind / look-ahead in fields (these decoders are per-field/frame
    // and don't need temporal context — both return 0).
    static qint32 getLookBehind() { return 0; }
    static qint32 getLookAhead() { return 0; }

protected:
    LdDecodeMetaData::VideoParameters videoParameters{};
    std::unique_ptr<Ort::Env> ortEnv;
    std::unique_ptr<Ort::Session> ortSession;
    Ort::AllocatorWithDefaultOptions ortAllocator;
    Mode mode = Mode::Field;
};

// ldzeug2_color_cnn: replaces steps 1+2 of the NTSC chain (Y/C separation
// and chroma demod) with a single inference. Per-field and weaved-frame
// model variants both work; mode is auto-detected.
class LdzeugColorCnnDecoder : public LdzeugDecoderBase {
public:
    void decodeFrames(const QVector<SourceField> &inputFields,
                      qint32 startIndex, qint32 endIndex,
                      QVector<ComponentFrame> &componentFrames) override;

    // Optional knobs that map to the matching kwargs on decode_4fsc_video.
    void setChromaPhase(double degrees) { chromaPhase = degrees; }
    void setChromaGain(double gain) { chromaGain = gain; }

private:
    double chromaPhase = 0.0;  // degrees
    double chromaGain = 1.0;
};

// ldzeug2_luma_sep: NN performs Y/C separation only; chroma is recovered
// analytically as ``CVBS - Y`` and run through a per-pixel I/Q demod and
// optional bandpass FIR. Mode auto-detected.
class LdzeugLumaSepDecoder : public LdzeugDecoderBase {
public:
    void decodeFrames(const QVector<SourceField> &inputFields,
                      qint32 startIndex, qint32 endIndex,
                      QVector<ComponentFrame> &componentFrames) override;

    void setChromaPhase(double degrees) { chromaPhase = degrees; }
    void setChromaGain(double gain) { chromaGain = gain; }
    // When true (default), run the c_colorlp_b 17-tap LP FIR on I and Q
    // after the per-pixel demod. Matches ldzeug2's comb_split_already
    // ``color_bp=True`` default.
    void setChromaBandpass(bool enable) { chromaBandpass = enable; }

private:
    double chromaPhase = 0.0;
    double chromaGain = 1.0;
    bool chromaBandpass = true;
};

#endif // LDZEUG_DECODERS_H