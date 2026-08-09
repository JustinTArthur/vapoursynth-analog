/******************************************************************************
 * chromadecsource.h
 * vapoursynth-analog - libchromadec (chd_*) lifecycle wrapper
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 ******************************************************************************/

#ifndef CHROMADECSOURCE_H
#define CHROMADECSOURCE_H

#include <filesystem>
#include <stdexcept>
#include <string>
#include <vector>

#include <chromadec/chromadec.h>

// Exception carrying a libchromadec / plugin error message.
class VSAnalogException : public std::runtime_error {
public:
    using std::runtime_error::runtime_error;
};

// RAII wrapper over the libchromadec C ABI: owns the chd_video_t, chd_decoder_t
// and optional chd_nn_model_t and drives the open -> configure -> commit ->
// decode lifecycle. Format/property policy lives one layer up in
// VSAnalog4fscSource; this class is pure libchromadec plumbing.
class ChromaDecSource {
public:
    ChromaDecSource();
    ~ChromaDecSource();
    ChromaDecSource(const ChromaDecSource &) = delete;
    ChromaDecSource &operator=(const ChromaDecSource &) = delete;

    // --- Open (exactly one of these) ---
    // override_or_null merges over the sidecar; used to force SECAM on a
    // 625-line capture whose sidecar mis-declares PAL.
    void openComposite(const std::filesystem::path &path,
                       const chd_video_params_t *override_or_null);
    void openYC(const std::filesystem::path &lumaPath,
                const std::filesystem::path &chromaPath,
                const chd_video_params_t *override_or_null);

    // Extra sources for multi-source dropout correction (after open).
    void addExtraComposite(const std::filesystem::path &path);
    void addExtraYC(const std::filesystem::path &lumaPath,
                    const std::filesystem::path &chromaPath);

    const chd_video_info_t &info() const { return vinfo_; }

    // --- Interface-standard numbering (after open) ---
    // Translate the analogue standards' numbering into the source-row numbering
    // the crop options take, for this source's own field height and horizontal
    // alignment. signalLine is the field-sequential line number of ST 170 /
    // BT.470 / BT.1700 / EBU Tech 3280; standardSample counts from the start of
    // the digital active line, per ST 244 / Tech 3280-E.
    int32_t signalLineToFrameLine(int32_t signalLine) const;
    int32_t standardSampleToRowSample(int32_t standardSample) const;

    // --- Configure (after open, before commit) ---
    void createDecoder(chd_decoder_kind_t kind);
    // Set an option, tolerating CHD_E_INVALID_ARG (option not meaningful for
    // the selected decoder kind); throws on any other failure.
    void setOptF64(const char *name, double v);
    void setOptI32(const char *name, int32_t v);
    void setOptBool(const char *name, bool v);
    void setOptStr(const char *name, const char *v);
    // Like setOpt* but a CHD_E_INVALID_ARG is also fatal (for options that must
    // apply, e.g. output_format / the active-window crop).
    void setOptStrRequired(const char *name, const char *v);
    void setOptI32Required(const char *name, int32_t v);

    void setDropout(bool enabled, bool overcorrect, bool intra);
    // Load the model with the requested backend and attach it. Falls back to
    // the ORT CPU EP if a pinned accelerator backend fails to load.
    void setNnModel(const std::string &modelPath, chd_nn_backend_t backend,
                    chd_nn_compute_precision_t precision);

    void commit();
    const chd_output_info_t &outputInfo() const { return oinfo_; }

    // --- Decode ---
    // Returns an owning chd_frame_t*; caller frees with chd_frame_free.
    chd_frame_t *decodeFrame(int64_t frameIndex);
    bool lastDropoutStats(chd_dropout_stats_t &out) const;
    // The frame's dropout spans, in the committed output framing. Reads
    // metadata rather than decoding, but the decoder's own concealment spans
    // are only cached once the frame has been decoded, so call this after
    // decodeFrame to see them.
    void dropoutSpans(int64_t frameIndex, bool overcorrect,
                      std::vector<chd_dropout_span_t> &out) const;

private:
    void afterOpen();  // populate vinfo_

    chd_video_t *v_ = nullptr;
    chd_decoder_t *d_ = nullptr;
    chd_nn_model_t *nn_ = nullptr;
    chd_video_info_t vinfo_{};
    chd_output_info_t oinfo_{};
};

#endif // CHROMADECSOURCE_H
