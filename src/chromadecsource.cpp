/******************************************************************************
 * chromadecsource.cpp
 * vapoursynth-analog - libchromadec (chd_*) lifecycle wrapper
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 ******************************************************************************/

#include "chromadecsource.h"

#include "chdlog.h"
#include "gpupreload.h"

#include <cstdlib>
#include <mutex>
#include <string>

namespace {

// chd_init() is process-global; run it exactly once.
void ensureChdInit() {
    static std::once_flag once;
    static chd_status_t initStatus = CHD_OK;
    std::call_once(once, [] { initStatus = chd_init(); });
    if (initStatus != CHD_OK) {
        throw VSAnalogException(std::string("chd_init failed: ") +
                                chd_status_str(initStatus));
    }
}

// libchromadec requires chd_shutdown() to run once before process exit *once any
// NN model has been loaded* — it tears down the ONNX Runtime environment, and
// skipping it risks a static-destruction crash. VapourSynth gives no plugin
// unload hook, so register it with atexit() the first time a model loads. When
// no NN decoder is used, chd_shutdown() is unnecessary and never registered.
void ensureShutdownRegistered() {
    static std::once_flag once;
    std::call_once(once, [] { std::atexit(chd_shutdown); });
}

// Clear the thread's libchromadec error detail before a call whose failure we
// are going to report. The detail is sticky — nothing resets it on success, and
// a failure the library handled internally leaves one behind, as our own
// tolerant setters do every time they skip an option — so without this an older
// message could be read back as this call's reason.
void beginChdCall() {
    chd_clear_last_error();
}

// Build an error message for a failed call. libchromadec's detail names the
// entry point and the input at fault, so it stands on its own; `what` is the
// fallback for a status that arrived without one.
std::string chdError(const char *what, chd_status_t s) {
    const char *detail = chd_last_error();
    if (detail && *detail) {
        return detail;
    }
    return std::string(what) + ": " + chd_status_str(s);
}

}  // namespace

ChromaDecSource::ChromaDecSource() {
    ensureChdInit();
}

ChromaDecSource::~ChromaDecSource() {
    // Order: decoder before video; model after decoder detaches.
    if (d_) {
        chd_decoder_free(d_);
        d_ = nullptr;
    }
    if (nn_) {
        chd_nn_model_free(nn_);
        nn_ = nullptr;
    }
    if (v_) {
        chd_video_free(v_);
        v_ = nullptr;
    }
}

void ChromaDecSource::afterOpen() {
    beginChdCall();
    chd_status_t s = chd_video_get_info(v_, &vinfo_);
    if (s != CHD_OK) {
        throw VSAnalogException(chdError("failed to read video info", s));
    }
}

void ChromaDecSource::openComposite(const std::filesystem::path &path,
                                    const chd_video_params_t *override_or_null) {
    beginChdCall();
    chd_status_t s = chd_video_open_composite(path.string().c_str(), nullptr,
                                              override_or_null, &v_);
    if (s != CHD_OK) {
        throw VSAnalogException(chdError("failed to open source", s));
    }
    afterOpen();
}

void ChromaDecSource::openYC(const std::filesystem::path &lumaPath,
                             const std::filesystem::path &chromaPath,
                             const chd_video_params_t *override_or_null) {
    beginChdCall();
    chd_status_t s = chd_video_open_yc(lumaPath.string().c_str(),
                                       chromaPath.string().c_str(), nullptr,
                                       override_or_null, &v_);
    if (s != CHD_OK) {
        throw VSAnalogException(chdError("failed to open Y/C source", s));
    }
    afterOpen();
}

void ChromaDecSource::addExtraComposite(const std::filesystem::path &path) {
    beginChdCall();
    chd_status_t s = chd_video_add_extra_source_composite(
        v_, path.string().c_str(), nullptr);
    if (s != CHD_OK) {
        throw VSAnalogException(chdError("failed to add extra source", s));
    }
}

void ChromaDecSource::addExtraYC(const std::filesystem::path &lumaPath,
                                 const std::filesystem::path &chromaPath) {
    beginChdCall();
    chd_status_t s = chd_video_add_extra_source_yc(
        v_, lumaPath.string().c_str(), chromaPath.string().c_str(), nullptr);
    if (s != CHD_OK) {
        throw VSAnalogException(chdError("failed to add extra Y/C source", s));
    }
}

int32_t ChromaDecSource::signalLineToFrameLine(int32_t signalLine) const {
    int32_t frameLine = 0;
    beginChdCall();
    chd_status_t s = chd_video_signal_line_to_frame_line(v_, signalLine, &frameLine);
    if (s != CHD_OK) {
        throw VSAnalogException(chdError("signal line conversion", s));
    }
    return frameLine;
}

int32_t ChromaDecSource::standardSampleToRowSample(int32_t standardSample) const {
    int32_t rowSample = 0;
    beginChdCall();
    chd_status_t s =
        chd_video_standard_sample_to_row_sample(v_, standardSample, &rowSample);
    if (s != CHD_OK) {
        throw VSAnalogException(chdError("standard sample conversion", s));
    }
    return rowSample;
}

void ChromaDecSource::createDecoder(chd_decoder_kind_t kind) {
    beginChdCall();
    chd_status_t s = chd_decoder_create(v_, kind, &d_);
    if (s != CHD_OK) {
        throw VSAnalogException(chdError("failed to create decoder", s));
    }
}

void ChromaDecSource::setOptF64(const char *name, double v) {
    beginChdCall();
    chd_status_t s = chd_decoder_set_option_f64(d_, name, v);
    if (s != CHD_OK && s != CHD_E_INVALID_ARG) {
        throw VSAnalogException(chdError(name, s));
    }
}

void ChromaDecSource::setOptI32(const char *name, int32_t v) {
    beginChdCall();
    chd_status_t s = chd_decoder_set_option_i32(d_, name, v);
    if (s != CHD_OK && s != CHD_E_INVALID_ARG) {
        throw VSAnalogException(chdError(name, s));
    }
}

void ChromaDecSource::setOptBool(const char *name, bool v) {
    beginChdCall();
    chd_status_t s = chd_decoder_set_option_bool(d_, name, v ? 1 : 0);
    if (s != CHD_OK && s != CHD_E_INVALID_ARG) {
        throw VSAnalogException(chdError(name, s));
    }
}

void ChromaDecSource::setOptStr(const char *name, const char *v) {
    beginChdCall();
    chd_status_t s = chd_decoder_set_option_str(d_, name, v);
    if (s != CHD_OK && s != CHD_E_INVALID_ARG) {
        throw VSAnalogException(chdError(name, s));
    }
}

void ChromaDecSource::setOptStrRequired(const char *name, const char *v) {
    beginChdCall();
    chd_status_t s = chd_decoder_set_option_str(d_, name, v);
    if (s != CHD_OK) {
        throw VSAnalogException(chdError(name, s));
    }
}

void ChromaDecSource::setOptI32Required(const char *name, int32_t v) {
    beginChdCall();
    chd_status_t s = chd_decoder_set_option_i32(d_, name, v);
    if (s != CHD_OK) {
        throw VSAnalogException(chdError(name, s));
    }
}

void ChromaDecSource::setDropout(bool enabled, bool overcorrect, bool intra) {
    chd_dropout_opts_t opts = {};
    opts.enabled = enabled ? 1 : 0;
    opts.overcorrect = overcorrect ? 1 : 0;
    opts.intra_field_only = intra ? 1 : 0;
    beginChdCall();
    chd_status_t s = chd_decoder_set_dropout(d_, &opts);
    if (s != CHD_OK) {
        throw VSAnalogException(chdError("dropout configuration", s));
    }
}

void ChromaDecSource::setNnModel(const std::string &modelPath,
                                 chd_nn_backend_t backend,
                                 chd_nn_compute_precision_t precision) {
    // The accelerated ONNX Runtime providers dlopen the vendor runtime when
    // the session is created below; resolve pip-installed copies first so
    // that works without LD_LIBRARY_PATH/PATH. CPU and native CoreML load
    // nothing vendor-side.
    if (backend != CHD_NN_ORT_CPU && backend != CHD_NN_COREML) {
        gpupreload::ensureLoaded();
    }

    chd_nn_session_opts_t opts;
    chd_nn_session_opts_default(&opts);
    opts.backend = backend;
    // For the native CoreML backend, use every available compute unit. The ANE
    // only runs fp16 programs, so this changes placement for the fp16-converted
    // nnTransform3D v2 package the Apple-silicon wheel bundles and is a no-op
    // for the fp32 ones. Ignored by the ONNX Runtime backends.
    opts.coreml_compute = CHD_NN_COREML_ALL;
    // libchromadec defaults to a single intra-op thread because its own
    // DecoderPool parallelises across frames. VapourSynth pulls one frame at a
    // time (fmUnordered), so let ONNX Runtime use the cores instead.
    opts.intra_op_threads = 0;
    // Only TensorRT acts on this, building a mixed fp16/fp32 engine; every
    // other backend runs the model at its stored precision. Engines built
    // either way are cached separately, so flipping it can't serve a stale one.
    opts.precision = precision;

    beginChdCall();
    chd_status_t s = chd_nn_model_load_from_file(modelPath.c_str(), &opts, &nn_);
    if (s != CHD_OK) {
        // Graceful fallback: a pinned accelerator EP that isn't available on
        // this host retries on the ORT CPU EP so the process survives (a
        // native CoreML request needs no fallback).
        //
        // Say so. libchromadec deliberately reports an unavailable pinned
        // backend rather than substituting one, and this retry overrides that
        // decision — silently, it would leave a user who asked for an
        // accelerator running an order of magnitude slower on the CPU with no
        // way to find out. `auto` never reaches here: its fallback happens
        // inside the library's own provider chain.
        if (backend != CHD_NN_ORT_CPU && backend != CHD_NN_COREML) {
            const std::string reason = chdError("NN backend unavailable", s);
            opts.backend = CHD_NN_ORT_CPU;
            beginChdCall();
            s = chd_nn_model_load_from_file(modelPath.c_str(), &opts, &nn_);
            if (s == CHD_OK) {
                chdlog::warn(reason + "; the requested onnx_provider could not "
                             "be used and this decode falls back to the CPU, "
                             "which is far slower. Pass onnx_provider=\"auto\" "
                             "to select the best provider this host can "
                             "actually run.");
            }
        }
        if (s != CHD_OK) {
            throw VSAnalogException(chdError("failed to load NN model", s));
        }
    }

    beginChdCall();
    s = chd_decoder_set_nn_model(d_, nn_);
    if (s != CHD_OK) {
        throw VSAnalogException(chdError("failed to attach NN model", s));
    }

    // ONNX Runtime env now exists; ensure it's torn down before process exit.
    ensureShutdownRegistered();
}

void ChromaDecSource::commit() {
    beginChdCall();
    chd_status_t s = chd_decoder_commit(d_);
    if (s != CHD_OK) {
        throw VSAnalogException(chdError("failed to commit decoder", s));
    }
    beginChdCall();
    s = chd_decoder_get_output_info(d_, &oinfo_);
    if (s != CHD_OK) {
        throw VSAnalogException(chdError("failed to read output info", s));
    }
}

chd_frame_t *ChromaDecSource::decodeFrame(int64_t frameIndex) {
    chd_frame_t *f = nullptr;
    beginChdCall();
    chd_status_t s = chd_decode_frame(d_, frameIndex, &f);
    if (s != CHD_OK) {
        throw VSAnalogException(chdError("failed to decode frame", s));
    }
    return f;
}

bool ChromaDecSource::lastDropoutStats(chd_dropout_stats_t &out) const {
    return chd_decoder_get_last_dropout_stats(d_, &out) == CHD_OK;
}

void ChromaDecSource::dropoutSpans(int64_t frameIndex, bool overcorrect,
                                   std::vector<chd_dropout_span_t> &out) const {
    chd_dropout_span_t *spans = nullptr;
    size_t count = 0;
    beginChdCall();
    chd_status_t s = chd_decoder_get_dropout_spans(
        d_, frameIndex,
        overcorrect ? CHD_DROPOUT_OVERCORRECT : CHD_DROPOUT_DETECTED,
        &spans, &count);
    if (s != CHD_OK) {
        throw VSAnalogException(chdError("failed to read dropout spans", s));
    }
    // A clean frame yields a null pointer, not an empty allocation.
    out.clear();
    if (count > 0) {
        out.assign(spans, spans + count);
    }
    chd_dropout_spans_free(spans);
}
