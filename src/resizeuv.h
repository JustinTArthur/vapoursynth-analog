/******************************************************************************
 * resizeuv.h
 * vapoursynth-analog - shared chroma-kernel options for internal resize passes
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 ******************************************************************************/

#ifndef RESIZEUV_H
#define RESIZEUV_H

#include <cstdint>
#include <optional>
#include <string>

#include <VapourSynth4.h>

namespace resizeuv {

// The resize plugin's own resample_filter_uv vocabulary, which resample_secam
// shares.
struct Options {
    std::string filter = "bicubic";
    std::optional<double> paramA;
    std::optional<double> paramB;
};

// Reads resample_filter_uv / filter_param_a_uv / filter_param_b_uv off a
// filter's arguments. The kernel name is validated here only for a full-menu
// error message; the value itself passes through to resize.
bool readOptions(const VSMap *in, Options &opts, std::string &err, const VSAPI *vsapi);

// Runs clip through the resize plugin to formatId, with the chroma kernel
// options forwarded verbatim — resize scopes them to the chroma planes itself.
// resize.Point is invoked because a main kernel must be named, but it applies
// to nothing: luma dimensions never change here. Consumes the clip reference;
// returns nullptr with err set on failure.
VSNode *toFormat(VSPlugin *resizePlugin, VSNode *clip, uint32_t formatId,
                 const Options &opts, std::optional<int> matrixIn,
                 std::optional<int> matrixOut, std::string &err, const VSAPI *vsapi);

}  // namespace resizeuv

#endif
