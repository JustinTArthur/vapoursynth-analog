/******************************************************************************
 * ldzeug_decoders.h
 * vapoursynth-analog - NN-based NTSC decoders derived from jsaowji/ldzeug2
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * This module supplies two neural-network-based NTSC decoders that bypass the
 * tbc-tools Comb pipeline entirely:
 *
 *   - LdzeugColorCnnDecoder: takes [CVBS, I-carrier, Q-carrier] per field
 *     (carriers analytically synthesized from sample position + fieldPhase)
 *     and emits [Y, I, Q] in one inference pass, replacing both Y/C
 *     separation and chroma demodulation.
 *
 *   - LdzeugLumaSepDecoder (luma-only at present): takes a CVBS field or
 *     frame and emits Y. The chroma demodulation half of the pipeline is
 *     a planned follow-up; in the meantime the decoder writes neutral
 *     U/V so output is effectively grayscale.
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

// Common base: holds the shared Ort env, session options, and per-instance
// session. Concrete subclasses implement decodeFrames().
class LdzeugDecoderBase {
public:
    virtual ~LdzeugDecoderBase() = default;

    // Update video parameters and (re)load the ONNX session from modelPath.
    // ``provider`` is the EP name to request (e.g. "cuda", "rocm", "coreml");
    // empty means platform default. Throws std::runtime_error on session load
    // failure.
    void configure(const LdDecodeMetaData::VideoParameters &videoParameters,
                   const QString &modelPath,
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
};

// ldzeug2_color_cnn: replaces steps 1+2 of the NTSC chain (Y/C separation
// and chroma demod) with a single inference. Bundled weights:
//   v1, v1_denoise — 64x16 ColorCNN, single-field input
//   v2             — 64x16 ColorCNN v2 (single branch w/ learned IQ mixers)
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

// ldzeug2_luma_sep: replaces step 1 only (Y/C separation). Bundled weights:
//   field — 32x14 compact CNN, per-field input
//   frame — same arch but trained on weaved frames
//
// The downstream chroma demod (ldzeug2's analytical comb_split_already) is
// not yet implemented; this decoder currently produces Y only and writes
// neutral U/V. Callers wanting color from luma_sep should track issue
// updates.
class LdzeugLumaSepDecoder : public LdzeugDecoderBase {
public:
    enum class Mode { Field, Frame };

    void setMode(Mode m) { mode = m; }

    void decodeFrames(const QVector<SourceField> &inputFields,
                      qint32 startIndex, qint32 endIndex,
                      QVector<ComponentFrame> &componentFrames) override;

private:
    Mode mode = Mode::Field;
};

// Translate the model_version tag selected by the Python wrapper into a
// LdzeugLumaSepDecoder::Mode. Versions are matched case-insensitively.
LdzeugLumaSepDecoder::Mode lumaSepModeFromModelPath(const QString &path);

#endif // LDZEUG_DECODERS_H