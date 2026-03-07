/******************************************************************************
 * dropoutcorrector.h
 * vapoursynth-analog - Dropout correction for TBC field data
 *
 * Adapted from ld-decode-tools' ld-dropout-correct by Simon Inns and
 * Adam Sampson, simplified for in-process use.
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 ******************************************************************************/

#ifndef DROPOUTCORRECTOR_H
#define DROPOUTCORRECTOR_H

#include "lddecodemetadata.h"
#include "sourcevideo.h"
#include "sourcefield.h"

struct DropoutCorrectionStats {
    int corrected = 0;      // Dropout regions successfully replaced
    int failed = 0;         // Dropout regions where no replacement was found
    int totalDistance = 0;   // Sum of spatial distances of all replacements
};

// Per-source frame data for multi-source correction.
// Each extra source provides its field data and metadata for one frame.
struct ExtraSourceFrame {
    SourceVideo::Data firstFieldData;
    SourceVideo::Data secondFieldData;
    LdDecodeMetaData::Field firstFieldMeta;
    LdDecodeMetaData::Field secondFieldMeta;
    LdDecodeMetaData::VideoParameters videoParams;
    double quality = -1.0;  // Frame quality (average bPSNR of both fields)
};

class DropoutCorrector {
public:
    explicit DropoutCorrector(const LdDecodeMetaData::VideoParameters &videoParams);

    // Single-source correction.
    // Modifies SourceField::data in place before chroma decoding.
    void correctFrame(SourceField &firstField, SourceField &secondField,
                      bool overCorrect, bool intraField,
                      DropoutCorrectionStats *stats = nullptr);

    // Multi-source correction.
    // Primary fields are modified in place. Extra sources provide replacement
    // data from additional captures aligned via VBI frame numbers.
    void correctFrame(SourceField &primaryFirst, SourceField &primarySecond,
                      const QVector<ExtraSourceFrame> &extraSources,
                      bool overCorrect, bool intraField,
                      DropoutCorrectionStats *stats = nullptr);

private:
    enum Location {
        visibleLine,
        colourBurst,
        unknown
    };

    struct DropOutLocation {
        qint32 fieldLine;
        qint32 startx;
        qint32 endx;
        Location location;
    };

    struct Replacement {
        Replacement() : isSameField(true), fieldLine(-1), sourceNumber(0), quality(-1.0), distance(0) {}

        bool isSameField;
        qint32 fieldLine;
        qint32 sourceNumber;
        double quality;
        qint32 distance;
    };

    LdDecodeMetaData::VideoParameters videoParameters;

    void correctField(const QVector<QVector<DropOutLocation>> &thisFieldDropouts,
                      const QVector<QVector<DropOutLocation>> &otherFieldDropouts,
                      QVector<SourceVideo::Data> &thisFieldData,
                      const QVector<SourceVideo::Data> &otherFieldData,
                      bool thisFieldIsFirst, bool intraField,
                      const QVector<qint32> &availableSources,
                      const QVector<double> &sourceQuality,
                      const QVector<LdDecodeMetaData::VideoParameters> &allVideoParams,
                      DropoutCorrectionStats *stats);

    QVector<DropOutLocation> populateDropoutsVector(const LdDecodeMetaData::Field &field,
                                                     const LdDecodeMetaData::VideoParameters &vp,
                                                     bool overCorrect);
    QVector<DropOutLocation> setDropOutLocations(QVector<DropOutLocation> dropOuts);

    Replacement findReplacementLine(const QVector<QVector<DropOutLocation>> &thisFieldDropouts,
                                    const QVector<QVector<DropOutLocation>> &otherFieldDropouts,
                                    qint32 dropOutIndex, bool thisFieldIsFirst,
                                    bool matchChromaPhase, bool isColourBurst,
                                    bool intraField,
                                    const QVector<qint32> &availableSources,
                                    const QVector<double> &sourceQuality,
                                    const QVector<LdDecodeMetaData::VideoParameters> &allVideoParams);

    void findPotentialReplacementLine(const QVector<QVector<DropOutLocation>> &targetDropouts,
                                      qint32 targetIndex,
                                      const QVector<QVector<DropOutLocation>> &sourceDropouts,
                                      bool isSameField,
                                      qint32 sourceOffset, qint32 stepAmount,
                                      qint32 sourceNo,
                                      const QVector<double> &sourceQuality,
                                      const QVector<LdDecodeMetaData::VideoParameters> &allVideoParams,
                                      QVector<Replacement> &candidates);

    void correctDropOut(const DropOutLocation &dropOut,
                        const Replacement &replacement,
                        const Replacement &chromaReplacement,
                        QVector<SourceVideo::Data> &thisFieldData,
                        const QVector<SourceVideo::Data> &otherFieldData);
};

#endif // DROPOUTCORRECTOR_H
