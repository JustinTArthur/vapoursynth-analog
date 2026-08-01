/******************************************************************************
 * modernizechromaticity.h
 * vapoursynth-analog - colorimetry/photometry modernization filter
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 ******************************************************************************/

#ifndef MODERNIZECHROMATICITY_H
#define MODERNIZECHROMATICITY_H

#include <VapourSynth4.h>

void VS_CC CreateModernizeChromaticity(const VSMap *In, VSMap *Out, void *userData,
                                       VSCore *Core, const VSAPI *vsapi);

#endif
