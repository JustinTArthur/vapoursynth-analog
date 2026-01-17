/******************************************************************************
 * analog4fsc.h
 * vapoursynth-analog - 4FSC video source for VapourSynth
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 ******************************************************************************/

#ifndef ANALOG4FSC_H
#define ANALOG4FSC_H

#include <filesystem>
#include <memory>
#include <mutex>
#include <cstdint>

class TbcReader;
class ComponentFrame;

// Video format description
struct VSAnalogVideoFormat {
    int ColorFamily;       // 1=Gray, 2=RGB, 3=YUV, 4=Bayer
    int SampleType;        // 0=Integer, 1=Float
    int BitsPerSample;
    int SubSamplingW;      // log2 horizontal subsampling (0 for 4:4:4)
    int SubSamplingH;      // log2 vertical subsampling (0 for 4:4:4)
};

// Rational number for time/fps
struct VSAnalogRational {
    int64_t Num;
    int64_t Den;

    double ToDouble() const { return static_cast<double>(Num) / static_cast<double>(Den); }
};

// Video properties
struct VSAnalogVideoProperties {
    VSAnalogVideoFormat VF;
    int Width;
    int Height;
    int SSModWidth;    // Width rounded to subsampling multiple
    int SSModHeight;   // Height rounded to subsampling multiple
    int64_t NumFrames;
    int64_t NumRFFFrames;  // Number of frames with RFF applied
    VSAnalogRational FPS;
    int64_t Duration;
    VSAnalogRational TimeBase;
};

// Decode options
struct VSAnalog4fscOptions {
    double chromaGain = 1.0;
    double chromaPhase = 0.0;
    double chromaNR = 0.0;         // Chroma noise reduction (NTSC only)
    double lumaNR = 0.0;           // Luma noise reduction (all decoders)
    int paddingMultiple = 8;       // Output padding multiple (0 = no padding)
    bool reverseFields = false;
    bool phaseCompensation = false; // NTSC phase compensation
    std::string decoder;           // Decoder name (empty = auto)
};

// Exception class for VSAnalog errors
class VSAnalogException : public std::runtime_error {
public:
    using std::runtime_error::runtime_error;
};

// Main 4FSC source class
class VSAnalog4fscSource {
public:
    // Single source (composite) or dual source (luma + chroma from separate TBCs)
    VSAnalog4fscSource(const std::filesystem::path &sourcePath,
                       const std::filesystem::path *chromaSourcePath,
                       const VSAnalog4fscOptions *opts);
    ~VSAnalog4fscSource();

    // Prevent copying
    VSAnalog4fscSource(const VSAnalog4fscSource &) = delete;
    VSAnalog4fscSource &operator=(const VSAnalog4fscSource &) = delete;

    // Get video properties
    const VSAnalogVideoProperties &GetVideoProperties() const { return properties; }

    // Check if using mono (grayscale) output
    bool IsMonoOutput() const;

    // Get first active frame line (for field order calculation)
    int GetFirstActiveFrameLine() const;

    // Check if video system is NTSC (or PAL-M) vs PAL
    bool IsNTSC() const;

    // Check if source is widescreen (16:9)
    bool IsWidescreen() const;

    // Get sample aspect ratio (for _SARNum/_SARDen frame properties)
    // Values match ld-chroma-decoder's outputwriter.cpp (EBU R92 / SMPTE RP 187)
    struct SampleAspectRatio { int num; int den; };
    SampleAspectRatio GetSAR() const;

    // Get video parameters for YCbCr scaling
    double GetBlack16bIre() const;
    double GetWhite16bIre() const;
    int GetActiveVideoStart() const;

    // Get active (unpadded) dimensions
    int GetActiveWidth() const;
    int GetActiveHeight() const;

    // Set seek pre-roll (for accurate seeking)
    void SetSeekPreRoll(int preroll);

    // Get a frame - writes YUV float data to the provided buffers
    // yData, uData, vData: pointers to output buffers (float)
    // yStride, uStride, vStride: strides in bytes
    // Returns true on success
    bool GetFrame(int frameNumber, float *yData, float *uData, float *vData,
                  int yStride, int uStride, int vStride);

private:
    std::unique_ptr<TbcReader> reader;        // Primary (luma/composite) source
    std::unique_ptr<TbcReader> chromaReader;  // Optional separate chroma source
    VSAnalogVideoProperties properties;
    int seekPreRoll = 0;
    std::mutex decodeMutex;  // Protect decoding (single-threaded access to ld-decode)

    void initProperties();
    void convertToFloat(const ComponentFrame &lumaFrame,
                        const ComponentFrame *chromaFrame,
                        float *yData, float *uData, float *vData,
                        int yStride, int uStride, int vStride);
};

#endif // ANALOG4FSC_H
