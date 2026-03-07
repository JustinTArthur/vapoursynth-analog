/******************************************************************************
 * tbcreader.cpp
 * vapoursynth-analog - TBC file reader wrapper
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 ******************************************************************************/

#include "tbcreader.h"
#include "jsonconverter_wrapper.h"
#include "sqlite3_metadata_reader.h"
#include "vbidecoder.h"

#include <QFileInfo>
#include <QDebug>

TbcReader::DecoderType TbcReader::parseDecoderName(const QString &name) {
    QString lower = name.toLower();
    if (lower == "ntsc1d") return DecoderType::Ntsc1D;
    if (lower == "ntsc2d") return DecoderType::Ntsc2D;
    if (lower == "ntsc3d") return DecoderType::Ntsc3D;
    if (lower == "ntsc3dnoadapt") return DecoderType::Ntsc3DNoAdapt;
    if (lower == "pal2d") return DecoderType::Pal2D;
    if (lower == "transform2d") return DecoderType::Transform2D;
    if (lower == "transform3d") return DecoderType::Transform3D;
    if (lower == "mono") return DecoderType::Mono;
    return DecoderType::Auto;
}

TbcReader::TbcReader()
    : metadata(std::make_unique<LdDecodeMetaData>())
    , sourceVideo(std::make_unique<SourceVideo>())
{
}

TbcReader::~TbcReader() {
    close();
}

bool TbcReader::openTbcSource(const QString &tbcPathStr,
                              LdDecodeMetaData &meta, SourceVideo &video) {
    // Find metadata file (.db or .json, converting JSON to SQLite if needed)
    QFileInfo tbcInfo(tbcPathStr);
    QString baseName = tbcInfo.absolutePath() + "/" + tbcInfo.completeBaseName();
    QString dbPath = baseName + ".db";

    if (!QFileInfo::exists(dbPath)) {
        dbPath = tbcPathStr + ".db";
    }

    if (!QFileInfo::exists(dbPath)) {
        QString jsonPath = baseName + ".json";
        if (!QFileInfo::exists(jsonPath)) jsonPath = tbcPathStr + ".json";
        if (!QFileInfo::exists(jsonPath)) jsonPath = baseName + ".tbc.json";

        if (QFileInfo::exists(jsonPath)) {
            qInfo() << "Found JSON metadata, converting to SQLite:" << jsonPath;
            if (!convertJsonToSqlite(jsonPath, dbPath)) {
                lastError = "Failed to convert JSON metadata to SQLite: " + jsonPath;
                return false;
            }
        } else {
            lastError = "Could not find metadata file (.db or .json): " + baseName;
            return false;
        }
    }

    if (!Sqlite3MetadataReader::read(dbPath, meta)) {
        lastError = "Failed to read metadata from: " + dbPath;
        return false;
    }

    auto vp = meta.getVideoParameters();
    if (!vp.isValid) {
        lastError = "Invalid video parameters in metadata";
        return false;
    }

    qint32 fieldLength = vp.fieldWidth * vp.fieldHeight;
    if (!video.open(tbcPathStr, fieldLength, vp.fieldWidth)) {
        lastError = "Failed to open TBC file: " + tbcPathStr;
        return false;
    }

    return true;
}

bool TbcReader::open(const std::filesystem::path &tbcPath, const Configuration &cfg) {
    close();
    config = cfg;

    QString tbcPathStr = QString::fromStdString(tbcPath.string());
    if (!openTbcSource(tbcPathStr, *metadata, *sourceVideo)) {
        return false;
    }

    videoParameters = metadata->getVideoParameters();

    // Configure the appropriate decoder
    if (!configureDecoder()) {
        return false;
    }

    // Calculate output dimensions (active video area only)
    activeWidth = videoParameters.activeVideoEnd - videoParameters.activeVideoStart;
    activeHeight = videoParameters.lastActiveFrameLine - videoParameters.firstActiveFrameLine;
    outputWidth = activeWidth;
    outputHeight = activeHeight;

    // Apply padding if specified (round up to multiple of paddingMultiple)
    if (config.paddingMultiple > 0) {
        int padW = config.paddingMultiple;
        int padH = config.paddingMultiple;
        outputWidth = ((outputWidth + padW - 1) / padW) * padW;
        outputHeight = ((outputHeight + padH - 1) / padH) * padH;
    }

    isOpen = true;
    return true;
}

bool TbcReader::configureDecoder() {
    // Determine which decoder to use
    DecoderType decoder = config.decoder;

    // Auto-select based on video system color carrier if not specified
    if (decoder == DecoderType::Auto) {
        switch (videoParameters.system) {
            case NTSC:
                decoder = DecoderType::Ntsc2D;
                break;
            case PAL:
            case PAL_M:
            default:
                decoder = DecoderType::Pal2D;
                break;
        }
    }

    // Validate decoder is appropriate for video system
    const bool isNtscColorCarrier = (videoParameters.system == NTSC);

    switch (decoder) {
        case DecoderType::Ntsc1D:
        case DecoderType::Ntsc2D:
        case DecoderType::Ntsc3D:
        case DecoderType::Ntsc3DNoAdapt:
            if (!isNtscColorCarrier) {
                qWarning() << "NTSC decoder selected but video color carrier is PAL; using PAL decoder instead";
                decoder = DecoderType::Pal2D;
            }
            break;
        case DecoderType::Pal2D:
        case DecoderType::Transform2D:
        case DecoderType::Transform3D:
            if (isNtscColorCarrier) {
                qWarning() << "PAL decoder selected but video color carrier is NTSC; using NTSC decoder instead";
                decoder = DecoderType::Ntsc2D;
            }
            break;
        case DecoderType::Mono:
        case DecoderType::Auto:
            // Mono works with any system, Auto already handled above
            break;
    }

    activeDecoder = decoder;

    // Configure the selected decoder
    switch (decoder) {
        case DecoderType::Ntsc1D:
        case DecoderType::Ntsc2D:
        case DecoderType::Ntsc3D:
        case DecoderType::Ntsc3DNoAdapt: {
            combFilter = std::make_unique<Comb>();
            Comb::Configuration combConfig;
            combConfig.chromaGain = config.chromaGain;
            combConfig.chromaPhase = config.chromaPhase;
            combConfig.cNRLevel = config.chromaNR;
            combConfig.yNRLevel = config.lumaNR;
            combConfig.phaseCompensation = config.phaseCompensation;

            switch (decoder) {
                case DecoderType::Ntsc1D:
                    combConfig.dimensions = 1;
                    combConfig.adaptive = false;
                    break;
                case DecoderType::Ntsc2D:
                    combConfig.dimensions = 2;
                    combConfig.adaptive = false;
                    break;
                case DecoderType::Ntsc3D:
                    combConfig.dimensions = 3;
                    combConfig.adaptive = true;
                    break;
                case DecoderType::Ntsc3DNoAdapt:
                    combConfig.dimensions = 3;
                    combConfig.adaptive = false;
                    break;
                default:
                    break;
            }

            combFilter->updateConfiguration(videoParameters, combConfig);
            lookBehind = combConfig.getLookBehind();
            lookAhead = combConfig.getLookAhead();
            qInfo() << "Using NTSC decoder:" << static_cast<int>(decoder)
                    << "dimensions:" << combConfig.dimensions
                    << "adaptive:" << combConfig.adaptive
                    << "phaseComp:" << combConfig.phaseCompensation
                    << "cNR:" << combConfig.cNRLevel
                    << "yNR:" << combConfig.yNRLevel;
            break;
        }

        case DecoderType::Pal2D:
        case DecoderType::Transform2D:
        case DecoderType::Transform3D: {
            palColour = std::make_unique<PalColour>();
            PalColour::Configuration palConfig;
            palConfig.chromaGain = config.chromaGain;
            palConfig.chromaPhase = config.chromaPhase;
            palConfig.yNRLevel = config.lumaNR;

            switch (decoder) {
                case DecoderType::Pal2D:
                    palConfig.chromaFilter = PalColour::palColourFilter;
                    break;
                case DecoderType::Transform2D:
                    palConfig.chromaFilter = PalColour::transform2DFilter;
                    break;
                case DecoderType::Transform3D:
                    palConfig.chromaFilter = PalColour::transform3DFilter;
                    break;
                default:
                    break;
            }

            palColour->updateConfiguration(videoParameters, palConfig);
            lookBehind = palConfig.getLookBehind();
            lookAhead = palConfig.getLookAhead();
            qInfo() << "Using PAL decoder:" << static_cast<int>(decoder)
                    << "filter:" << static_cast<int>(palConfig.chromaFilter)
                    << "yNR:" << palConfig.yNRLevel;
            break;
        }

        case DecoderType::Mono: {
            monoDecoder = std::make_unique<MonoDecoder>();
            MonoDecoder::MonoConfiguration monoConfig;
            monoConfig.videoParameters = videoParameters;
            monoConfig.yNRLevel = config.lumaNR;
            monoDecoder->updateConfiguration(videoParameters, monoConfig);
            lookBehind = 0;
            lookAhead = 0;
            qInfo() << "Using Mono decoder"
                    << "yNR:" << monoConfig.yNRLevel;
            break;
        }

        case DecoderType::Auto:
            // Should not reach here
            lastError = "Failed to auto-select decoder";
            return false;
    }

    return true;
}

void TbcReader::close() {
    if (isOpen) {
        sourceVideo->close();
        metadata->clear();
        extraSources.clear();
        primaryVbiScanned = false;
        isOpen = false;
    }
}

bool TbcReader::addExtraSource(const std::filesystem::path &tbcPath) {
    if (!isOpen) {
        lastError = "Primary source must be opened before adding extra sources";
        return false;
    }

    // Scan primary VBI range on first extra source addition
    if (!primaryVbiScanned) {
        primaryVbiAvailable = scanVbiFrameRange(*metadata, primaryDiscTypeCav,
                                                primaryMinVbiFrame, primaryMaxVbiFrame);
        primaryVbiScanned = true;
        if (primaryVbiAvailable) {
            qInfo() << "Primary source VBI range:" << primaryMinVbiFrame << "-" << primaryMaxVbiFrame
                    << (primaryDiscTypeCav ? "(CAV)" : "(CLV)");
        } else {
            qInfo() << "Primary source has no VBI frame numbers; using sequential alignment for extra sources";
        }
    }

    ExtraSource extra;
    extra.metadata = std::make_unique<LdDecodeMetaData>();
    extra.sourceVideo = std::make_unique<SourceVideo>();

    QString tbcPathStr = QString::fromStdString(tbcPath.string());
    if (!openTbcSource(tbcPathStr, *extra.metadata, *extra.sourceVideo)) {
        return false;
    }

    // Scan VBI frame range (optional — falls back to sequential alignment)
    extra.vbiAvailable = scanVbiFrameRange(*extra.metadata, extra.discTypeCav,
                                            extra.minVbiFrame, extra.maxVbiFrame);
    if (extra.vbiAvailable) {
        qInfo() << "Extra source" << extraSources.size() << "VBI range:"
                << extra.minVbiFrame << "-" << extra.maxVbiFrame
                << (extra.discTypeCav ? "(CAV)" : "(CLV)");
    } else {
        qInfo() << "Extra source" << extraSources.size()
                << "has no VBI frame numbers; using sequential alignment ("
                << extra.metadata->getNumberOfFrames() << "frames)";
    }

    extraSources.push_back(std::move(extra));
    return true;
}

int TbcReader::getWidth() const {
    return outputWidth;
}

int TbcReader::getHeight() const {
    return outputHeight;
}

int TbcReader::getActiveWidth() const {
    return activeWidth;
}

int TbcReader::getActiveHeight() const {
    return activeHeight;
}

int TbcReader::getNumFrames() const {
    return metadata->getNumberOfFrames();
}

VideoSystem TbcReader::getVideoSystem() const {
    return videoParameters.system;
}

TbcReader::FrameRate TbcReader::getFrameRate() const {
    // Return standard frame rates based on video system
    switch (videoParameters.system) {
        case NTSC:
        case PAL_M:
            return {30000, 1001};  // 29.97 fps
        case PAL:
        default:
            return {25, 1};
    }
}

// Scan all frames in a metadata source to determine VBI frame range.
// Adapted from CorrectorPool::setMinAndMaxVbiFrames().
bool TbcReader::scanVbiFrameRange(LdDecodeMetaData &meta, bool &isCav,
                                   qint32 &minFrame, qint32 &maxFrame) {
    VbiDecoder vbiDecoder;
    qint32 cavCount = 0, clvCount = 0;
    qint32 cavMin = 1000000, cavMax = 0;
    qint32 clvMin = 1000000, clvMax = 0;

    for (qint32 seqFrame = 1; seqFrame <= meta.getNumberOfFrames(); seqFrame++) {
        auto vbi1 = meta.getFieldVbi(meta.getFirstFieldNumber(seqFrame)).vbiData;
        auto vbi2 = meta.getFieldVbi(meta.getSecondFieldNumber(seqFrame)).vbiData;
        VbiDecoder::Vbi vbi = vbiDecoder.decodeFrame(
            vbi1[0], vbi1[1], vbi1[2], vbi2[0], vbi2[1], vbi2[2]);

        if (vbi.picNo > 0) {
            cavCount++;
            if (vbi.picNo < cavMin) cavMin = vbi.picNo;
            if (vbi.picNo > cavMax) cavMax = vbi.picNo;
        }

        if (vbi.clvHr != -1 && vbi.clvMin != -1 &&
            vbi.clvSec != -1 && vbi.clvPicNo != -1) {
            clvCount++;
            LdDecodeMetaData::ClvTimecode timecode;
            timecode.hours = vbi.clvHr;
            timecode.minutes = vbi.clvMin;
            timecode.seconds = vbi.clvSec;
            timecode.pictureNumber = vbi.clvPicNo;
            qint32 cvFrame = meta.convertClvTimecodeToFrameNumber(timecode);
            if (cvFrame < clvMin) clvMin = cvFrame;
            if (cvFrame > clvMax) clvMax = cvFrame;
        }
    }

    if (cavCount == 0 && clvCount == 0) {
        return false;
    }

    if (cavCount > clvCount) {
        isCav = true;
        minFrame = cavMin;
        maxFrame = cavMax;
    } else {
        isCav = false;
        minFrame = clvMin;
        maxFrame = clvMax;
    }

    return true;
}

qint32 TbcReader::vbiToSequential(qint32 vbiFrame, qint32 minVbiFrame) {
    return vbiFrame - minVbiFrame + 1;
}

qint32 TbcReader::sequentialToVbi(qint32 seqFrame, qint32 minVbiFrame) {
    return (minVbiFrame - 1) + seqFrame;
}

void TbcReader::loadExtraSourceFrames(int frameNumber,
                                       QVector<ExtraSourceFrame> &extras) {
    extras.clear();
    if (extraSources.empty()) return;

    // frameNumber is 0-based; sequential frame numbers are 1-based
    qint32 primarySeq = frameNumber + 1;

    // Compute primary VBI frame number if VBI alignment is available
    qint32 primaryVbi = primaryVbiAvailable
        ? sequentialToVbi(primarySeq, primaryMinVbiFrame) : 0;

    for (size_t i = 0; i < extraSources.size(); i++) {
        ExtraSource &src = extraSources[i];

        qint32 extraSeq;
        if (primaryVbiAvailable && src.vbiAvailable) {
            // VBI alignment: map primary VBI → extra sequential
            if (primaryVbi < src.minVbiFrame || primaryVbi > src.maxVbiFrame) continue;
            extraSeq = vbiToSequential(primaryVbi, src.minVbiFrame);
        } else {
            // Sequential alignment: same frame number, clamped to range
            extraSeq = primarySeq;
        }
        if (extraSeq < 1 || extraSeq > src.metadata->getNumberOfFrames()) continue;

        qint32 firstFieldNo = src.metadata->getFirstFieldNumber(extraSeq);
        qint32 secondFieldNo = src.metadata->getSecondFieldNumber(extraSeq);

        // Skip padded (missing) frames
        if (src.metadata->getField(firstFieldNo).pad &&
            src.metadata->getField(secondFieldNo).pad) continue;

        ExtraSourceFrame esf;
        esf.videoParams = src.metadata->getVideoParameters();

        // Load field data (read in TBC sequential order to minimize seeking)
        if (firstFieldNo < secondFieldNo) {
            esf.firstFieldData = src.sourceVideo->getVideoField(firstFieldNo);
            esf.secondFieldData = src.sourceVideo->getVideoField(secondFieldNo);
        } else {
            esf.secondFieldData = src.sourceVideo->getVideoField(secondFieldNo);
            esf.firstFieldData = src.sourceVideo->getVideoField(firstFieldNo);
        }

        esf.firstFieldMeta = src.metadata->getField(firstFieldNo);
        esf.secondFieldMeta = src.metadata->getField(secondFieldNo);

        // Quality from average bPSNR
        esf.quality = (esf.firstFieldMeta.vitsMetrics.bPSNR
                      + esf.secondFieldMeta.vitsMetrics.bPSNR) / 2.0;

        extras.append(std::move(esf));
    }
}

bool TbcReader::loadFieldsForFrame(int frameNumber, QVector<SourceField> &fields,
                                    qint32 &startIndex, qint32 &endIndex) {
    // Load fields using SourceField's static method
    // Frame numbers are 1-based in ld-decode
    SourceField::loadFields(*sourceVideo, *metadata,
                           frameNumber + 1,  // Convert to 1-based
                           1,                 // Number of frames
                           lookBehind,
                           lookAhead,
                           fields,
                           startIndex, endIndex);

    return fields.size() > 0;
}

bool TbcReader::decodeFrame(int frameNumber, ComponentFrame &frame,
                            DropoutCorrectionStats *stats) {
    if (!isOpen) {
        lastError = "TBC file not open";
        return false;
    }

    if (frameNumber < 0 || frameNumber >= getNumFrames()) {
        lastError = "Frame number out of range";
        return false;
    }

    // Load fields for this frame (and any look-behind/ahead needed)
    QVector<SourceField> fields;
    qint32 startIndex = 0, endIndex = 0;

    if (!loadFieldsForFrame(frameNumber, fields, startIndex, endIndex)) {
        lastError = "Failed to load fields for frame " + QString::number(frameNumber);
        return false;
    }

    // Handle field reversal if requested
    if (config.reverseFields && fields.size() >= 2) {
        // Swap the field order within each frame pair
        for (int i = startIndex; i < endIndex; i += 2) {
            if (i + 1 < fields.size()) {
                std::swap(fields[i], fields[i + 1]);
            }
        }
    }

    // Apply dropout correction to the raw TBC field data before chroma decoding
    if (config.dropoutCorrect && (startIndex + 1) < fields.size()) {
        DropoutCorrector corrector(videoParameters);
        if (!extraSources.empty()) {
            QVector<ExtraSourceFrame> extras;
            loadExtraSourceFrames(frameNumber, extras);
            corrector.correctFrame(fields[startIndex], fields[startIndex + 1],
                                   extras, config.dropoutOvercorrect,
                                   config.dropoutIntra, stats);
        } else {
            corrector.correctFrame(fields[startIndex], fields[startIndex + 1],
                                   config.dropoutOvercorrect, config.dropoutIntra,
                                   stats);
        }
    }

    // Initialize output frame
    QVector<ComponentFrame> componentFrames;
    componentFrames.resize(1);
    componentFrames[0].init(videoParameters);

    // Decode using the appropriate decoder
    switch (activeDecoder) {
        case DecoderType::Ntsc1D:
        case DecoderType::Ntsc2D:
        case DecoderType::Ntsc3D:
        case DecoderType::Ntsc3DNoAdapt:
            combFilter->decodeFrames(fields, startIndex, endIndex, componentFrames);
            break;

        case DecoderType::Pal2D:
        case DecoderType::Transform2D:
        case DecoderType::Transform3D:
            palColour->decodeFrames(fields, startIndex, endIndex, componentFrames);
            break;

        case DecoderType::Mono:
            monoDecoder->decodeFrames(fields, startIndex, endIndex, componentFrames);
            break;

        case DecoderType::Auto:
            lastError = "Decoder not configured";
            return false;
    }

    frame = std::move(componentFrames[0]);
    return true;
}
