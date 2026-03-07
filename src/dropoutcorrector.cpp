/******************************************************************************
 * dropoutcorrector.cpp
 * vapoursynth-analog - Dropout correction for TBC field data
 *
 * Adapted from ld-decode-tools' ld-dropout-correct (dropoutcorrect.cpp)
 * by Simon Inns and Adam Sampson. Simplified for in-process use without
 * QThread/CorrectorPool dependencies.
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 ******************************************************************************/

#include "dropoutcorrector.h"
#include "filters.h"

DropoutCorrector::DropoutCorrector(const LdDecodeMetaData::VideoParameters &videoParams)
    : videoParameters(videoParams)
{
}

// Single-source convenience: delegates to multi-source with no extras
void DropoutCorrector::correctFrame(SourceField &firstField, SourceField &secondField,
                                     bool overCorrect, bool intraField,
                                     DropoutCorrectionStats *stats)
{
    correctFrame(firstField, secondField, {}, overCorrect, intraField, stats);
}

// Multi-source correction
void DropoutCorrector::correctFrame(SourceField &primaryFirst, SourceField &primarySecond,
                                     const QVector<ExtraSourceFrame> &extraSources,
                                     bool overCorrect, bool intraField,
                                     DropoutCorrectionStats *stats)
{
    // Determine broadcast field order from metadata
    SourceField &broadcastFirst = primaryFirst.field.isFirstField ? primaryFirst : primarySecond;
    SourceField &broadcastSecond = primaryFirst.field.isFirstField ? primarySecond : primaryFirst;

    // Build total source count: primary (0) + extras (1..N)
    const qint32 totalSources = 1 + extraSources.size();

    // Collect all field data indexed by source [source][samples]
    QVector<SourceVideo::Data> allFirstFieldData(totalSources);
    QVector<SourceVideo::Data> allSecondFieldData(totalSources);
    QVector<LdDecodeMetaData::Field> allFirstFieldMeta(totalSources);
    QVector<LdDecodeMetaData::Field> allSecondFieldMeta(totalSources);
    QVector<LdDecodeMetaData::VideoParameters> allVideoParams(totalSources);
    QVector<double> sourceQuality(totalSources);

    // Source 0 = primary
    allFirstFieldData[0] = broadcastFirst.data;
    allSecondFieldData[0] = broadcastSecond.data;
    allFirstFieldMeta[0] = broadcastFirst.field;
    allSecondFieldMeta[0] = broadcastSecond.field;
    allVideoParams[0] = videoParameters;
    // Compute primary quality from VITS bPSNR
    sourceQuality[0] = (broadcastFirst.field.vitsMetrics.bPSNR
                       + broadcastSecond.field.vitsMetrics.bPSNR) / 2.0;

    // Sources 1..N = extras
    for (qint32 i = 0; i < extraSources.size(); i++) {
        allFirstFieldData[i + 1] = extraSources[i].firstFieldData;
        allSecondFieldData[i + 1] = extraSources[i].secondFieldData;
        allFirstFieldMeta[i + 1] = extraSources[i].firstFieldMeta;
        allSecondFieldMeta[i + 1] = extraSources[i].secondFieldMeta;
        allVideoParams[i + 1] = extraSources[i].videoParams;
        sourceQuality[i + 1] = extraSources[i].quality;
    }

    // Determine which sources are available (all of them, since TbcReader
    // only passes extras that have the required frame)
    QVector<qint32> availableSources;
    for (qint32 i = 0; i < totalSources; i++) {
        availableSources.append(i);
    }

    // Skip if no dropouts in primary fields
    if (allFirstFieldMeta[0].dropOuts.empty() && allSecondFieldMeta[0].dropOuts.empty()) {
        return;
    }

    // Build dropout location vectors for all sources
    QVector<QVector<DropOutLocation>> firstFieldDropouts(totalSources);
    QVector<QVector<DropOutLocation>> secondFieldDropouts(totalSources);

    for (qint32 i = 0; i < totalSources; i++) {
        if (!allFirstFieldMeta[i].dropOuts.empty()) {
            firstFieldDropouts[i] = setDropOutLocations(
                populateDropoutsVector(allFirstFieldMeta[i], allVideoParams[i], overCorrect));
        }
        if (!allSecondFieldMeta[i].dropOuts.empty()) {
            secondFieldDropouts[i] = setDropOutLocations(
                populateDropoutsVector(allSecondFieldMeta[i], allVideoParams[i], overCorrect));
        }
    }

    // Correct both fields
    correctField(firstFieldDropouts, secondFieldDropouts,
                 allFirstFieldData, allSecondFieldData,
                 true, intraField, availableSources, sourceQuality,
                 allVideoParams, stats);

    correctField(secondFieldDropouts, firstFieldDropouts,
                 allSecondFieldData, allFirstFieldData,
                 false, intraField, availableSources, sourceQuality,
                 allVideoParams, stats);

    // Write corrected primary data back
    broadcastFirst.data = allFirstFieldData[0];
    broadcastSecond.data = allSecondFieldData[0];
}

void DropoutCorrector::correctField(const QVector<QVector<DropOutLocation>> &thisFieldDropouts,
                                     const QVector<QVector<DropOutLocation>> &otherFieldDropouts,
                                     QVector<SourceVideo::Data> &thisFieldData,
                                     const QVector<SourceVideo::Data> &otherFieldData,
                                     bool thisFieldIsFirst, bool intraField,
                                     const QVector<qint32> &availableSources,
                                     const QVector<double> &sourceQuality,
                                     const QVector<LdDecodeMetaData::VideoParameters> &allVideoParams,
                                     DropoutCorrectionStats *stats)
{
    for (qint32 dropoutIndex = 0; dropoutIndex < thisFieldDropouts[0].size(); dropoutIndex++) {
        Replacement replacement, chromaReplacement;

        if (thisFieldDropouts[0][dropoutIndex].location == Location::colourBurst) {
            replacement = findReplacementLine(thisFieldDropouts, otherFieldDropouts,
                                              dropoutIndex, thisFieldIsFirst, true,
                                              true, intraField, availableSources,
                                              sourceQuality, allVideoParams);
        }

        if (thisFieldDropouts[0][dropoutIndex].location == Location::visibleLine) {
            replacement = findReplacementLine(thisFieldDropouts, otherFieldDropouts,
                                              dropoutIndex, thisFieldIsFirst, false,
                                              false, intraField, availableSources,
                                              sourceQuality, allVideoParams);
            chromaReplacement = findReplacementLine(thisFieldDropouts, otherFieldDropouts,
                                                    dropoutIndex, thisFieldIsFirst, true,
                                                    false, intraField, availableSources,
                                                    sourceQuality, allVideoParams);
        }

        if (stats) {
            if (replacement.fieldLine == -1) {
                stats->failed++;
            } else {
                stats->corrected++;
                stats->totalDistance += replacement.distance;
            }
        }

        correctDropOut(thisFieldDropouts[0][dropoutIndex], replacement, chromaReplacement,
                       thisFieldData, otherFieldData);
    }
}

QVector<DropoutCorrector::DropOutLocation> DropoutCorrector::populateDropoutsVector(
    const LdDecodeMetaData::Field &field,
    const LdDecodeMetaData::VideoParameters &vp,
    bool overCorrect)
{
    QVector<DropOutLocation> fieldDropOuts;

    for (qint32 dropOutIndex = 0; dropOutIndex < field.dropOuts.size(); dropOutIndex++) {
        DropOutLocation dropOutLocation;
        dropOutLocation.startx = field.dropOuts.startx(dropOutIndex);
        dropOutLocation.endx = field.dropOuts.endx(dropOutIndex);
        dropOutLocation.fieldLine = field.dropOuts.fieldLine(dropOutIndex);
        dropOutLocation.location = Location::unknown;

        if (dropOutLocation.fieldLine < 1 || dropOutLocation.fieldLine > vp.fieldHeight) {
            continue;
        }

        if (overCorrect) {
            qint32 overCorrectionDots = 24;
            if (dropOutLocation.startx > overCorrectionDots)
                dropOutLocation.startx -= overCorrectionDots;
            else
                dropOutLocation.startx = 0;
            if (dropOutLocation.endx < vp.fieldWidth - overCorrectionDots)
                dropOutLocation.endx += overCorrectionDots;
            else
                dropOutLocation.endx = vp.fieldWidth;
        }

        fieldDropOuts.append(dropOutLocation);
    }

    return fieldDropOuts;
}

QVector<DropoutCorrector::DropOutLocation> DropoutCorrector::setDropOutLocations(
    QVector<DropOutLocation> dropOuts)
{
    qint32 splitCount = 0;

    do {
        qint32 noOfDropOuts = dropOuts.size();
        splitCount = 0;

        for (qint32 index = 0; index < noOfDropOuts; index++) {
            if (dropOuts[index].startx <= videoParameters.colourBurstEnd) {
                dropOuts[index].location = Location::colourBurst;

                if (dropOuts[index].endx > videoParameters.colourBurstEnd) {
                    DropOutLocation tempDropOut;
                    tempDropOut.startx = videoParameters.colourBurstEnd + 1;
                    tempDropOut.endx = dropOuts[index].endx;
                    tempDropOut.fieldLine = dropOuts[index].fieldLine;
                    tempDropOut.location = Location::colourBurst;
                    dropOuts.append(tempDropOut);

                    dropOuts[index].endx = videoParameters.colourBurstEnd;
                    splitCount++;
                }
            }
            else if (dropOuts[index].startx > videoParameters.colourBurstEnd &&
                     dropOuts[index].startx <= videoParameters.activeVideoEnd) {
                dropOuts[index].location = Location::visibleLine;

                if (dropOuts[index].endx > videoParameters.activeVideoEnd) {
                    dropOuts[index].endx = videoParameters.activeVideoEnd;
                    splitCount++;
                }
            }
        }
    } while (splitCount != 0);

    return dropOuts;
}

DropoutCorrector::Replacement DropoutCorrector::findReplacementLine(
    const QVector<QVector<DropOutLocation>> &thisFieldDropouts,
    const QVector<QVector<DropOutLocation>> &otherFieldDropouts,
    qint32 dropOutIndex, bool thisFieldIsFirst, bool matchChromaPhase,
    bool isColourBurst, bool intraField,
    const QVector<qint32> &availableSources,
    const QVector<double> &sourceQuality,
    const QVector<LdDecodeMetaData::VideoParameters> &allVideoParams)
{
    qint32 stepAmount, otherFieldOffset;
    if (!matchChromaPhase) {
        stepAmount = 1;
        otherFieldOffset = -1;
    } else if (videoParameters.system == PAL || videoParameters.system == PAL_M) {
        stepAmount = 4;
        otherFieldOffset = thisFieldIsFirst ? -3 : -1;
    } else {
        stepAmount = 2;
        otherFieldOffset = -1;
    }

    QVector<Replacement> candidates;

    for (qint32 i = 0; i < availableSources.size(); i++) {
        qint32 currentSource = availableSources[i];

        // Search within the same field (up and down)
        findPotentialReplacementLine(thisFieldDropouts, dropOutIndex,
                                     thisFieldDropouts, true, 0, -stepAmount,
                                     currentSource, sourceQuality, allVideoParams,
                                     candidates);
        findPotentialReplacementLine(thisFieldDropouts, dropOutIndex,
                                     thisFieldDropouts, true, stepAmount, stepAmount,
                                     currentSource, sourceQuality, allVideoParams,
                                     candidates);

        // Search the other field (not colour burst, not intra-field)
        if (!isColourBurst && !intraField) {
            findPotentialReplacementLine(thisFieldDropouts, dropOutIndex,
                                         otherFieldDropouts, false, otherFieldOffset, -stepAmount,
                                         currentSource, sourceQuality, allVideoParams,
                                         candidates);
            findPotentialReplacementLine(thisFieldDropouts, dropOutIndex,
                                         otherFieldDropouts, false, otherFieldOffset + stepAmount, stepAmount,
                                         currentSource, sourceQuality, allVideoParams,
                                         candidates);
        }
    }

    Replacement replacement;

    if (!candidates.empty()) {
        replacement.distance = 1000000;
        replacement.quality = -1;

        for (const Replacement &candidate : candidates) {
            const qint32 dropoutFrameLine = (2 * thisFieldDropouts[0][dropOutIndex].fieldLine)
                                            + (thisFieldIsFirst ? 0 : 1);
            const qint32 sourceFrameLine = (2 * candidate.fieldLine)
                                           + (candidate.isSameField
                                              ? (thisFieldIsFirst ? 0 : 1)
                                              : (thisFieldIsFirst ? 1 : 0));
            const qint32 distance = qAbs(dropoutFrameLine - sourceFrameLine);

            if (distance < replacement.distance) {
                replacement = candidate;
                replacement.distance = distance;
            } else if (distance == replacement.distance && candidate.quality > replacement.quality) {
                replacement = candidate;
                replacement.distance = distance;
            }
        }
    }

    return replacement;
}

void DropoutCorrector::findPotentialReplacementLine(
    const QVector<QVector<DropOutLocation>> &targetDropouts, qint32 targetIndex,
    const QVector<QVector<DropOutLocation>> &sourceDropouts, bool isSameField,
    qint32 sourceOffset, qint32 stepAmount,
    qint32 sourceNo, const QVector<double> &sourceQuality,
    const QVector<LdDecodeMetaData::VideoParameters> &allVideoParams,
    QVector<Replacement> &candidates)
{
    qint32 sourceLine = targetDropouts[0][targetIndex].fieldLine + sourceOffset;

    if ((sourceLine - 1) < allVideoParams[sourceNo].firstActiveFieldLine
        || (sourceLine - 1) >= allVideoParams[sourceNo].lastActiveFieldLine) {
        return;
    }

    while ((sourceLine - 1) >= allVideoParams[sourceNo].firstActiveFieldLine
           && sourceLine < allVideoParams[sourceNo].lastActiveFieldLine) {
        bool hasOverlap = false;
        for (qint32 sourceIndex = 0; sourceIndex < sourceDropouts[sourceNo].size(); sourceIndex++) {
            if (sourceDropouts[sourceNo][sourceIndex].fieldLine == sourceLine &&
                (targetDropouts[0][targetIndex].endx - sourceDropouts[sourceNo][sourceIndex].startx) >= 0 &&
                (sourceDropouts[sourceNo][sourceIndex].endx - targetDropouts[0][targetIndex].startx) >= 0) {
                sourceLine += stepAmount;
                hasOverlap = true;
                break;
            }
        }
        if (!hasOverlap) {
            Replacement replacement;
            replacement.isSameField = isSameField;
            replacement.fieldLine = sourceLine;
            replacement.sourceNumber = sourceNo;
            replacement.quality = sourceQuality[sourceNo];
            candidates.push_back(replacement);
            return;
        }
    }
}

void DropoutCorrector::correctDropOut(const DropOutLocation &dropOut,
                                       const Replacement &replacement,
                                       const Replacement &chromaReplacement,
                                       QVector<SourceVideo::Data> &thisFieldData,
                                       const QVector<SourceVideo::Data> &otherFieldData)
{
    if (replacement.fieldLine == -1) {
        return;
    }

    const quint16 *sourceLine = (replacement.isSameField
                                 ? thisFieldData[replacement.sourceNumber].data()
                                 : otherFieldData[replacement.sourceNumber].data())
                                + ((replacement.fieldLine - 1) * videoParameters.fieldWidth);
    quint16 *targetLine = thisFieldData[0].data()
                          + ((dropOut.fieldLine - 1) * videoParameters.fieldWidth);

    if ((chromaReplacement.fieldLine == -1) ||
        ((dropOut.fieldLine == replacement.fieldLine) &&
         (dropOut.fieldLine == chromaReplacement.fieldLine))) {
        for (qint32 pixel = dropOut.startx; pixel < dropOut.endx; pixel++) {
            targetLine[pixel] = sourceLine[pixel];
        }
    } else {
        Filters filters;
        QVector<quint16> lineBuf(videoParameters.fieldWidth);
        auto filterLineBuf = [&] {
            if (videoParameters.system == PAL) {
                filters.palLumaFirFilter(lineBuf.data(), lineBuf.size());
            } else if (videoParameters.system == NTSC) {
                filters.ntscLumaFirFilter(lineBuf.data(), lineBuf.size());
            } else {
                filters.palMLumaFirFilter(lineBuf.data(), lineBuf.size());
            }
        };

        // Extract LF from luma replacement
        for (qint32 pixel = 0; pixel < videoParameters.fieldWidth; pixel++) {
            lineBuf[pixel] = sourceLine[pixel];
        }
        filterLineBuf();
        for (qint32 pixel = dropOut.startx; pixel < dropOut.endx; pixel++) {
            targetLine[pixel] = lineBuf[pixel];
        }

        // Extract HF from chroma replacement (original minus LF)
        const quint16 *chromaLine = (chromaReplacement.isSameField
                                     ? thisFieldData[chromaReplacement.sourceNumber].data()
                                     : otherFieldData[chromaReplacement.sourceNumber].data())
                                    + ((chromaReplacement.fieldLine - 1) * videoParameters.fieldWidth);
        for (qint32 pixel = 0; pixel < videoParameters.fieldWidth; pixel++) {
            lineBuf[pixel] = chromaLine[pixel];
        }
        filterLineBuf();
        for (qint32 pixel = dropOut.startx; pixel < dropOut.endx; pixel++) {
            targetLine[pixel] += chromaLine[pixel] - lineBuf[pixel];
        }
    }
}
