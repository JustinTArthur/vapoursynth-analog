/******************************************************************************
 * tbcreader.h
 * vapoursynth-analog - TBC file reader wrapper
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 ******************************************************************************/

#ifndef TBCREADER_H
#define TBCREADER_H

#include <QString>
#include <QVector>
#include <memory>
#include <vector>
#include <filesystem>

#include "lddecodemetadata.h"
#include "sourcevideo.h"
#include "sourcefield.h"
#include "componentframe.h"
#include "comb.h"
#include "palcolour.h"
#include "monodecoder.h"
#include "dropoutcorrector.h"

// TBC file reader that wraps ld-decode-tools' TBC library
class TbcReader {
public:
    // Decoder types matching ld-chroma-decoder command line options
    enum class DecoderType {
        // NTSC decoders (use Comb filter)
        Ntsc1D,
        Ntsc2D,
        Ntsc3D,
        Ntsc3DNoAdapt,
        // PAL decoders (use PalColour)
        Pal2D,
        Transform2D,
        Transform3D,
        // Mono decoder (luma only)
        Mono,
        // Auto-select based on video system
        Auto
    };

    struct Configuration {
        double chromaGain = 1.0;
        double chromaPhase = 0.0;
        double chromaNR = 0.0;           // Chroma noise reduction (NTSC only)
        double lumaNR = 0.0;             // Luma noise reduction (all decoders)
        int paddingMultiple = 8;         // Output padding multiple (0 = no padding)
        bool reverseFields = false;
        bool phaseCompensation = false;  // NTSC phase compensation
        bool dropoutCorrect = false;     // Enable dropout correction
        bool dropoutOvercorrect = false; // Extend dropout boundaries (±24 samples)
        bool dropoutIntra = false;       // Intra-field only correction
        DecoderType decoder = DecoderType::Auto;
    };

    // Parse decoder name string (as used by ld-chroma-decoder CLI)
    // Returns Auto if the name is not recognized
    static DecoderType parseDecoderName(const QString &name);

    TbcReader();
    ~TbcReader();

    // Open a TBC file and its metadata
    bool open(const std::filesystem::path &tbcPath, const Configuration &config);
    void close();

    struct FrameRate {
        int64_t num;
        int64_t den;
    };

    // Get video properties
    int getWidth() const;
    int getHeight() const;
    int getActiveWidth() const;   // Width before padding
    int getActiveHeight() const;  // Height before padding
    int getNumFrames() const;
    VideoSystem getVideoSystem() const;
    FrameRate getFrameRate() const;
    bool isMonoDecoder() const { return activeDecoder == DecoderType::Mono; }
    bool isWidescreen() const { return videoParameters.isWidescreen; }
    int getFirstActiveFrameLine() const { return videoParameters.firstActiveFrameLine; }

    // Get video parameters for YCbCr scaling (black/white IRE levels)
    double getBlack16bIre() const { return static_cast<double>(videoParameters.black16bIre); }
    double getWhite16bIre() const { return static_cast<double>(videoParameters.white16bIre); }

    // Get active region offsets (for extracting from ComponentFrame)
    int getActiveVideoStart() const { return videoParameters.activeVideoStart; }

    // Add an extra source for multi-source dropout correction.
    // Extra sources are aligned to the primary via VBI frame numbers.
    // Returns true on success. Must be called after open().
    bool addExtraSource(const std::filesystem::path &tbcPath);

    // Decode a frame to Y'CbCr (returns ComponentFrame with Y, U, V planes)
    // If stats is non-null, accumulates dropout correction statistics.
    bool decodeFrame(int frameNumber, ComponentFrame &frame,
                     DropoutCorrectionStats *stats = nullptr);

    // Get the last error message
    QString getLastError() const { return lastError; }

private:
    std::unique_ptr<LdDecodeMetaData> metadata;
    std::unique_ptr<SourceVideo> sourceVideo;

    // Extra sources for multi-source dropout correction
    struct ExtraSource {
        std::unique_ptr<LdDecodeMetaData> metadata;
        std::unique_ptr<SourceVideo> sourceVideo;
        bool vbiAvailable = false;
        bool discTypeCav = false;
        qint32 minVbiFrame = 0;
        qint32 maxVbiFrame = 0;
    };
    std::vector<ExtraSource> extraSources;

    // VBI frame alignment for multi-source dropout correction.
    // If VBI data is unavailable, falls back to sequential alignment.
    bool primaryVbiScanned = false;
    bool primaryVbiAvailable = false;
    bool primaryDiscTypeCav = false;
    qint32 primaryMinVbiFrame = 0;
    qint32 primaryMaxVbiFrame = 0;

    // Decoders - only one will be active at a time
    std::unique_ptr<Comb> combFilter;          // For NTSC
    std::unique_ptr<PalColour> palColour;      // For PAL
    std::unique_ptr<MonoDecoder> monoDecoder;  // For mono

    DecoderType activeDecoder = DecoderType::Auto;
    LdDecodeMetaData::VideoParameters videoParameters;
    Configuration config;
    QString lastError;
    bool isOpen = false;

    // Cached frame dimensions
    int outputWidth = 0;
    int outputHeight = 0;
    int activeWidth = 0;   // Width before padding
    int activeHeight = 0;  // Height before padding

    // Look-behind/look-ahead for current decoder
    qint32 lookBehind = 0;
    qint32 lookAhead = 0;

    // Helper to load fields for a frame
    bool loadFieldsForFrame(int frameNumber, QVector<SourceField> &fields,
                            qint32 &startIndex, qint32 &endIndex);

    // Open a TBC's metadata (.db or .json→.db) and video file into the given objects
    bool openTbcSource(const QString &tbcPathStr,
                       LdDecodeMetaData &meta, SourceVideo &video);

    // Configure the appropriate decoder based on video system and settings
    bool configureDecoder();

    // VBI alignment helpers for multi-source dropout correction
    bool scanVbiFrameRange(LdDecodeMetaData &meta, bool &isCav,
                           qint32 &minFrame, qint32 &maxFrame);
    qint32 vbiToSequential(qint32 vbiFrame, qint32 minVbiFrame);
    qint32 sequentialToVbi(qint32 seqFrame, qint32 minVbiFrame);

    // Load extra source fields for a given primary frame number
    // and build the ExtraSourceFrame vector for DropoutCorrector
    void loadExtraSourceFrames(int frameNumber,
                               QVector<ExtraSourceFrame> &extras);
};

#endif // TBCREADER_H
