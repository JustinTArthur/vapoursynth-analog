/******************************************************************************
 * resizeuv.cpp
 * vapoursynth-analog - shared chroma-kernel options for internal resize passes
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 ******************************************************************************/

#include "resizeuv.h"

#include <algorithm>
#include <cctype>

namespace resizeuv {

namespace {

constexpr const char *kFilters[] = {
    "point", "bilinear", "bicubic", "spline16", "spline36", "spline64", "lanczos",
};

std::string filterNames() {
    std::string names;
    for (const char *name : kFilters) {
        if (!names.empty())
            names += ", ";
        names += name;
    }
    return names;
}

}  // namespace

bool readOptions(const VSMap *in, Options &opts, std::string &err, const VSAPI *vsapi) {
    int mapErr;
    if (const char *raw = vsapi->mapGetData(in, "resample_filter_uv", 0, &mapErr);
        !mapErr && raw) {
        std::string value(raw);
        std::transform(value.begin(), value.end(), value.begin(),
                       [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
        if (std::find(std::begin(kFilters), std::end(kFilters), value) ==
            std::end(kFilters)) {
            err = "resample_filter_uv value '" + value +
                  "' not recognized; expected one of: " + filterNames();
            return false;
        }
        opts.filter = value;
    }

    const double a = vsapi->mapGetFloat(in, "filter_param_a_uv", 0, &mapErr);
    if (!mapErr)
        opts.paramA = a;
    const double b = vsapi->mapGetFloat(in, "filter_param_b_uv", 0, &mapErr);
    if (!mapErr)
        opts.paramB = b;
    return true;
}

VSNode *toFormat(VSPlugin *resizePlugin, VSNode *clip, uint32_t formatId,
                 const Options &opts, std::optional<int> matrixIn,
                 std::optional<int> matrixOut, std::string &err, const VSAPI *vsapi) {
    VSMap *args = vsapi->createMap();
    vsapi->mapConsumeNode(args, "clip", clip, maAppend);
    vsapi->mapSetInt(args, "format", formatId, maAppend);
    vsapi->mapSetData(args, "resample_filter_uv", opts.filter.c_str(), -1, dtUtf8, maAppend);
    if (opts.paramA)
        vsapi->mapSetFloat(args, "filter_param_a_uv", *opts.paramA, maAppend);
    if (opts.paramB)
        vsapi->mapSetFloat(args, "filter_param_b_uv", *opts.paramB, maAppend);
    if (matrixIn)
        vsapi->mapSetInt(args, "matrix_in", *matrixIn, maAppend);
    if (matrixOut)
        vsapi->mapSetInt(args, "matrix", *matrixOut, maAppend);

    VSMap *ret = vsapi->invoke(resizePlugin, "Point", args);
    vsapi->freeMap(args);
    if (const char *invokeErr = vsapi->mapGetError(ret)) {
        err = invokeErr;
        vsapi->freeMap(ret);
        return nullptr;
    }
    VSNode *out = vsapi->mapGetNode(ret, "clip", 0, nullptr);
    vsapi->freeMap(ret);
    return out;
}

}  // namespace resizeuv
