/******************************************************************************
 * tbcreader.cpp
 * vapoursynth-analog - TBC file reader wrapper
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 ******************************************************************************/

#include "tbcreader.h"
#include "jsonconverter_wrapper.h"
#include "sqlite3_metadata_reader.h"

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

bool TbcReader::open(const std::filesystem::path &tbcPath, const Configuration &cfg) {
    close();
    config = cfg;

    QString tbcPathStr = QString::fromStdString(tbcPath.string());

    // Try to find the metadata file (.tbc.db or just .db alongside .tbc)
    QFileInfo tbcInfo(tbcPathStr);
    QString baseName = tbcInfo.absolutePath() + "/" + tbcInfo.completeBaseName();
    QString dbPath = baseName + ".db";

    // Try .tbc.db first (if filename was something.tbc)
    if (!QFileInfo::exists(dbPath)) {
        dbPath = tbcPathStr + ".db";
    }

    // If no .db file exists, check for JSON metadata and convert it
    if (!QFileInfo::exists(dbPath)) {
        // Try common JSON metadata file patterns
        QString jsonPath = baseName + ".json";
        if (!QFileInfo::exists(jsonPath)) {
            jsonPath = tbcPathStr + ".json";
        }
        if (!QFileInfo::exists(jsonPath)) {
            // Try .tbc.json pattern
            jsonPath = baseName + ".tbc.json";
        }

        if (QFileInfo::exists(jsonPath)) {
            qInfo() << "Found JSON metadata, converting to SQLite:" << jsonPath;

            // Convert JSON to SQLite
            if (!convertJsonToSqlite(jsonPath, dbPath)) {
                lastError = "Failed to convert JSON metadata to SQLite: " + jsonPath;
                return false;
            }
        } else {
            lastError = "Could not find metadata file (.db or .json): " + baseName;
            return false;
        }
    }

    // Read metadata using our sqlite3-based reader (avoids Qt SQL symbol conflicts)
    if (!Sqlite3MetadataReader::read(dbPath, *metadata)) {
        lastError = "Failed to read metadata from: " + dbPath;
        return false;
    }

    videoParameters = metadata->getVideoParameters();
    if (!videoParameters.isValid) {
        lastError = "Invalid video parameters in metadata";
        return false;
    }

    // Open the TBC video file
    qint32 fieldLength = videoParameters.fieldWidth * videoParameters.fieldHeight;
    if (!sourceVideo->open(tbcPathStr, fieldLength, videoParameters.fieldWidth)) {
        lastError = "Failed to open TBC file: " + tbcPathStr;
        return false;
    }

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
        isOpen = false;
    }
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

bool TbcReader::decodeFrame(int frameNumber, ComponentFrame &frame) {
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
    qint32 startIndex, endIndex;

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
