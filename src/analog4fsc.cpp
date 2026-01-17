/******************************************************************************
 * analog4fsc.cpp
 * vapoursynth-analog - 4FSC video source for VapourSynth
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 ******************************************************************************/

#include "analog4fsc.h"
#include "tbcreader.h"
#include "componentframe.h"

#include <stdexcept>

VSAnalog4fscSource::VSAnalog4fscSource(const std::filesystem::path &sourcePath,
                                        const std::filesystem::path *chromaSourcePath,
                                        const VSAnalog4fscOptions *opts)
    : reader(std::make_unique<TbcReader>())
{
    TbcReader::Configuration config;
    if (opts) {
        config.chromaGain = opts->chromaGain;
        config.chromaPhase = opts->chromaPhase;
        config.chromaNR = opts->chromaNR;
        config.lumaNR = opts->lumaNR;
        config.paddingMultiple = opts->paddingMultiple;
        config.reverseFields = opts->reverseFields;
        config.phaseCompensation = opts->phaseCompensation;
        if (!opts->decoder.empty()) {
            config.decoder = TbcReader::parseDecoderName(
                QString::fromStdString(opts->decoder));
        }
    }

    if (!reader->open(sourcePath, config)) {
        throw VSAnalogException("Failed to open TBC file: " +
                                reader->getLastError().toStdString());
    }

    // Open separate chroma source if provided (for color-under formats like VHS)
    if (chromaSourcePath) {
        chromaReader = std::make_unique<TbcReader>();
        if (!chromaReader->open(*chromaSourcePath, config)) {
            throw VSAnalogException("Failed to open chroma TBC file: " +
                                    chromaReader->getLastError().toStdString());
        }

        // Validate that both sources have compatible dimensions
        if (reader->getWidth() != chromaReader->getWidth() ||
            reader->getHeight() != chromaReader->getHeight()) {
            throw VSAnalogException("Luma and chroma TBC files have mismatched dimensions");
        }
        if (reader->getNumFrames() != chromaReader->getNumFrames()) {
            throw VSAnalogException("Luma and chroma TBC files have different frame counts");
        }
    }

    initProperties();
}

VSAnalog4fscSource::~VSAnalog4fscSource() = default;

bool VSAnalog4fscSource::IsMonoOutput() const {
    return reader->isMonoDecoder();
}

int VSAnalog4fscSource::GetFirstActiveFrameLine() const {
    return reader->getFirstActiveFrameLine();
}

bool VSAnalog4fscSource::IsNTSC() const {
    VideoSystem system = reader->getVideoSystem();
    return (system == NTSC || system == PAL_M);
}

bool VSAnalog4fscSource::IsWidescreen() const {
    return reader->isWidescreen();
}

VSAnalog4fscSource::SampleAspectRatio VSAnalog4fscSource::GetSAR() const {
    // Follow's ld-chroma-decoder current Y4M output, which is based on EBU R92
    // and SMPTE RP 187 (scaled from BT.601 (13.5 MHz) to 4fSC).
    // It's not clear how prolific RP 187 was in the industry, so consider
    // the NTSC ratios subject to change
    bool isNtsc = IsNTSC();
    bool widescreen = IsWidescreen();

    if (!isNtsc) {
        // PAL
        if (widescreen) {
            return {865, 779};  // (16/9) * (576 / (702 * 4*fSC / 13.5))
        } else {
            return {259, 311};  // (4/3) * (576 / (702 * 4*fSC / 13.5))
        }
    } else {
        // NTSC / PAL-M
        if (widescreen) {
            return {25, 22};    // (16/9) * (480 / (708 * 4*fSC / 13.5))
        } else {
            return {352, 413}; // (4/3) * (480 / (708 * 4*fSC / 13.5))
        }
    }
}

double VSAnalog4fscSource::GetBlack16bIre() const {
    return reader->getBlack16bIre();
}

double VSAnalog4fscSource::GetWhite16bIre() const {
    return reader->getWhite16bIre();
}

int VSAnalog4fscSource::GetActiveVideoStart() const {
    return reader->getActiveVideoStart();
}

int VSAnalog4fscSource::GetActiveWidth() const {
    return reader->getActiveWidth();
}

int VSAnalog4fscSource::GetActiveHeight() const {
    return reader->getActiveHeight();
}

void VSAnalog4fscSource::initProperties() {
    // Set up video format based on decoder type
    // With separate chroma source, we always output YUV even if luma decoder is mono
    if (reader->isMonoDecoder() && !chromaReader) {
        // Mono decoder outputs grayscale (GRAYS format)
        properties.VF.ColorFamily = 1;  // Gray
    } else {
        // Color decoders (or luma+chroma dual source) output YUV444PS
        properties.VF.ColorFamily = 3;  // YUV
    }
    properties.VF.SampleType = 1;       // Float
    properties.VF.BitsPerSample = 32;
    properties.VF.SubSamplingW = 0;     // No subsampling
    properties.VF.SubSamplingH = 0;

    properties.Width = reader->getWidth();
    properties.Height = reader->getHeight();
    properties.SSModWidth = properties.Width;
    properties.SSModHeight = properties.Height;
    properties.NumFrames = reader->getNumFrames();
    properties.NumRFFFrames = properties.NumFrames;  // No RFF support yet

    // Set frame rate based on video system
    auto fps = reader->getFrameRate();
    properties.FPS.Num = fps.num;
    properties.FPS.Den = fps.den;

    // Duration in timebase units (1/fps)
    properties.TimeBase.Num = properties.FPS.Den;
    properties.TimeBase.Den = properties.FPS.Num;
    properties.Duration = properties.NumFrames;
}

void VSAnalog4fscSource::SetSeekPreRoll(int preroll) {
    seekPreRoll = preroll;
}

bool VSAnalog4fscSource::GetFrame(int frameNumber, float *yData, float *uData, float *vData,
                                   int yStride, int uStride, int vStride) {
    std::lock_guard<std::mutex> lock(decodeMutex);

    ComponentFrame lumaFrame;
    if (!reader->decodeFrame(frameNumber, lumaFrame)) {
        return false;
    }

    // If we have a separate chroma source, decode from it too
    if (chromaReader) {
        ComponentFrame chromaFrame;
        if (!chromaReader->decodeFrame(frameNumber, chromaFrame)) {
            return false;
        }
        convertToFloat(lumaFrame, &chromaFrame, yData, uData, vData, yStride, uStride, vStride);
    } else {
        convertToFloat(lumaFrame, nullptr, yData, uData, vData, yStride, uStride, vStride);
    }
    return true;
}

void VSAnalog4fscSource::convertToFloat(const ComponentFrame &lumaFrame,
                                         const ComponentFrame *chromaFrame,
                                         float *yData, float *uData, float *vData,
                                         int yStride, int uStride, int vStride) {
    const int width = properties.Width;
    const int height = properties.Height;
    const int activeWidth = reader->getActiveWidth();
    const int activeHeight = reader->getActiveHeight();
    const bool isMono = (uData == nullptr);

    // Active region offsets (ComponentFrame contains full field data)
    const int firstActiveLine = reader->getFirstActiveFrameLine();
    const int activeVideoStart = reader->getActiveVideoStart();

    // For chroma from separate source, use its offsets (should match but be safe)
    const int chromaFirstActiveLine = chromaReader ? chromaReader->getFirstActiveFrameLine() : firstActiveLine;
    const int chromaActiveVideoStart = chromaReader ? chromaReader->getActiveVideoStart() : activeVideoStart;

    // Y'CbCr scaling constants from ld-chroma-decoder outputwriter.cpp
    // [Poynton ch25 p305] [BT.601-7 sec 2.5.3]
    static constexpr double Y_SCALE = 219.0 * 256.0;   // 56064
    static constexpr double C_SCALE = 112.0 * 256.0;   // 28672

    // ITU-R BT.601-7 [Poynton eq 25.1 p303 and eq 25.5 p307]
    static constexpr double ONE_MINUS_Kb = 1.0 - 0.114;  // 0.886
    static constexpr double ONE_MINUS_Kr = 1.0 - 0.299;  // 0.701

    // kB = sqrt(209556997.0 / 96146491.0) / 3.0
    // kR = sqrt(221990474.0 / 288439473.0)
    // [Poynton eq 28.1 p336]
    static constexpr double kB = 0.49211104112248356308804691718185;
    static constexpr double kR = 0.87728321993817866838972487283129;

    // Derive scaling factors from video parameters
    const double yOffset = reader->getBlack16bIre();
    const double yRange = reader->getWhite16bIre() - yOffset;
    const double uvRange = yRange;

    // Calculate scale factors (same as outputwriter.cpp YUV444P16)
    const double yScale = Y_SCALE / yRange;
    const double cbScale = (C_SCALE / (ONE_MINUS_Kb * kB)) / uvRange;
    const double crScale = (C_SCALE / (ONE_MINUS_Kr * kR)) / uvRange;

    // Normalization: scale to float range
    // Y: [0, Y_SCALE] -> [0, 1]
    // Cb/Cr: centered at 0, scale to approximately [-0.5, 0.5]
    const double yNorm = 1.0 / Y_SCALE;
    const double cbNorm = 1.0 / (2.0 * C_SCALE / (ONE_MINUS_Kb * kB));
    const double crNorm = 1.0 / (2.0 * C_SCALE / (ONE_MINUS_Kr * kR));

    // Determine which frame to use for chroma (separate chroma source or same as luma)
    const ComponentFrame &uvSourceFrame = chromaFrame ? *chromaFrame : lumaFrame;
    const int uvFirstActiveLine = chromaFrame ? chromaFirstActiveLine : firstActiveLine;
    const int uvActiveVideoStart = chromaFrame ? chromaActiveVideoStart : activeVideoStart;

    for (int y = 0; y < height; y++) {
        auto *yRow = reinterpret_cast<float *>(reinterpret_cast<uint8_t *>(yData) + y * yStride);

        if (y < activeHeight) {
            // Access ComponentFrame at the correct input line (with firstActiveLine offset)
            const double *srcY = lumaFrame.y(firstActiveLine + y) + activeVideoStart;

            for (int x = 0; x < activeWidth; x++) {
                // Y: subtract yOffset and multiply by yScale, normalize to [0, 1]
                yRow[x] = static_cast<float>((srcY[x] - yOffset) * yScale * yNorm);
            }
            // Fill horizontal padding with black (Y=0)
            for (int x = activeWidth; x < width; x++) {
                yRow[x] = 0.0f;
            }
        } else {
            // Fill vertical padding with black (Y=0)
            for (int x = 0; x < width; x++) {
                yRow[x] = 0.0f;
            }
        }

        // For color output, also convert U/V planes
        if (!isMono) {
            auto *uRow = reinterpret_cast<float *>(reinterpret_cast<uint8_t *>(uData) + y * uStride);
            auto *vRow = reinterpret_cast<float *>(reinterpret_cast<uint8_t *>(vData) + y * vStride);

            if (y < activeHeight) {
                // Get chroma from the appropriate source (separate chroma TBC or same as luma)
                const double *srcU = uvSourceFrame.u(uvFirstActiveLine + y) + uvActiveVideoStart;
                const double *srcV = uvSourceFrame.v(uvFirstActiveLine + y) + uvActiveVideoStart;

                for (int x = 0; x < activeWidth; x++) {
                    // Cb/Cr: multiply by scale, normalize to approximately [-0.5, 0.5]
                    uRow[x] = static_cast<float>(srcU[x] * cbScale * cbNorm);
                    vRow[x] = static_cast<float>(srcV[x] * crScale * crNorm);
                }
                // Fill horizontal padding with neutral chroma (U=V=0)
                for (int x = activeWidth; x < width; x++) {
                    uRow[x] = 0.0f;
                    vRow[x] = 0.0f;
                }
            } else {
                // Fill vertical padding with neutral chroma (U=V=0)
                for (int x = 0; x < width; x++) {
                    uRow[x] = 0.0f;
                    vRow[x] = 0.0f;
                }
            }
        }
    }
}
