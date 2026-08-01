/******************************************************************************
 * analog4fsc.h
 * vapoursynth-analog - 4FSC video source for VapourSynth
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 ******************************************************************************/

#ifndef ANALOG4FSC_H
#define ANALOG4FSC_H

#include <cstdint>
#include <filesystem>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <vector>

#include "chromadecsource.h"

// Decode options parsed from the plugin call.
struct VSAnalog4fscOptions {
    std::string decoder;           // Decoder name (empty = auto)

    double chromaGain = 1.0;
    double chromaPhase = 0.0;
    double chromaNR = 0.0;         // Chroma noise reduction (NTSC only)
    double lumaNR = 0.0;           // Luma noise reduction
    bool reverseFields = false;

    // Active-region crop, inclusive, in the interface standards' own numbering:
    // samples count from the start of the digital active line (ST 244 / EBU
    // Tech 3280-E, negative for the line blanking ahead of it) and lines are
    // the field-sequential signal line numbers (ST 170, BT.470, BT.1700, EBU
    // Tech 3280). Unset selects the standard's own active window.
    std::optional<int> firstActiveSample;
    std::optional<int> lastActiveSample;
    std::optional<int> firstActiveLine;
    std::optional<int> lastActiveLine;
    // Burst-locked chroma demodulation (NTSC).
    bool phaseCompensation = true;

    // Output selection / color science knobs.
    std::string colorFamily;       // "", "yuv", "rgb", "gray"
    std::string chromaFilter;      // "" = decoder default
    std::string colorDiffPrecision;        // "" = default ("modern")
    std::string broadcastScalingPrecision; // "" = default ("scientific")

    // Dropout correction.
    bool dropoutCorrect = false;
    bool dropoutOvercorrect = false;
    bool dropoutIntra = false;
    // Report each frame's dropout regions as a frame property. Independent of
    // correction; dropoutOvercorrect widens what gets reported, matching the
    // footprint correction would touch.
    bool annotateDropouts = false;
    std::vector<std::filesystem::path> dropoutExtraLumaSources;
    std::vector<std::filesystem::path> dropoutExtraChromaSources;

    // Neural-network decoders.
    std::string modelPath;         // Resolved model file (.onnx / .mlpackage)
    std::string onnxProvider;      // Execution provider name (empty = default)
    bool modelChromaBandpass = true;
    double modelInputScale = 1.0;
};

// Rational number for time/fps.
struct VSAnalogRational {
    int64_t Num;
    int64_t Den;
};

// Main 4FSC source: owns a ChromaDecSource and translates it into a VapourSynth
// clip (format, geometry, frame properties, float plane data).
class VSAnalog4fscSource {
public:
    // VapourSynth output family for this clip.
    enum class VSFormat { Gray, YUV444, YUV440, RGB };

    VSAnalog4fscSource(const std::filesystem::path &sourcePath,
                       const std::filesystem::path *chromaSourcePath,
                       const VSAnalog4fscOptions *opts);
    ~VSAnalog4fscSource();

    VSAnalog4fscSource(const VSAnalog4fscSource &) = delete;
    VSAnalog4fscSource &operator=(const VSAnalog4fscSource &) = delete;

    // Geometry / timing.
    VSFormat GetVSFormat() const { return vsFormat; }
    int GetWidth() const { return width; }
    int GetHeight() const { return height; }
    int64_t GetNumFrames() const { return numFrames; }
    VSAnalogRational GetFPS() const { return fps; }

    // Frame-property inputs.
    bool IsNTSCChromaticity() const { return isNtscChromaticity; }
    bool IsSecam() const { return isSecam; }
    bool UsesClassicColorDifference() const { return classicColorDifference; }
    // The resolved crop, in the interface standards' numbering.
    int GetFirstActiveLine() const { return firstActiveLine; }
    int GetLastActiveLine() const { return lastActiveLine; }
    int GetFirstActiveSample() const { return firstActiveSample; }
    int GetLastActiveSample() const { return lastActiveSample; }
    // Field 1 is the top field and sits on the even woven lines, so a crop that
    // starts on an odd one puts field 2 on top.
    bool IsBottomFieldFirst() const { return firstActiveFrameLine % 2 == 1; }
    struct SampleAspectRatio { int num; int den; };
    SampleAspectRatio GetSAR() const;
    bool DropoutEnabled() const { return dropoutCorrect; }

    // Per-frame extra outputs alongside the pixel data.
    struct FrameExtra {
        bool hasDropoutStats = false;
        int64_t dropoutCorrected = 0;
        int64_t dropoutFailed = 0;
        int64_t dropoutTotalDistance = 0;
        // Distinct from an empty span list, which is a genuinely clean frame.
        bool hasDropoutSpans = false;
        std::vector<chd_dropout_span_t> dropoutSpans;
        bool hasSecamComponent = false;
        int secamFirstRowComponent = 0;  // 0 = Db, 1 = Dr
    };

    // Decode frame n into the provided VS plane buffers. planeData/planeStride
    // hold one entry per plane (1 for Gray, 3 for YUV/RGB); strides are in bytes.
    void GetFrame(int frameNumber, float *const *planeData,
                  const int *planeStride, int numPlanes, FrameExtra &extra);

private:
    void configure(const std::filesystem::path &sourcePath,
                   const std::filesystem::path *chromaSourcePath,
                   const VSAnalog4fscOptions *opts);

    std::unique_ptr<ChromaDecSource> src;
    // A single chd_decoder_t can't take concurrent decode calls (it parallelises
    // each frame internally via its own pool); serialise access here.
    std::mutex decodeMutex;

    VSFormat vsFormat = VSFormat::YUV444;
    int width = 0;
    int height = 0;
    int64_t numFrames = 0;
    VSAnalogRational fps{30000, 1001};
    bool isNtscChromaticity = true;
    bool isSecam = false;
    bool classicColorDifference = false;
    bool isWidescreen = false;
    // The resolved crop, kept in both numberings: the standards' for reporting
    // and the source-row equivalents libchromadec is configured with.
    int firstActiveSample = 0;
    int lastActiveSample = 0;
    int firstActiveLine = 0;
    int lastActiveLine = 0;
    int firstActiveFrameLine = 0;
    bool dropoutCorrect = false;
    bool dropoutOvercorrect = false;
    bool annotateDropouts = false;
};

#endif // ANALOG4FSC_H
