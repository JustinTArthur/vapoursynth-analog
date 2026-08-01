/******************************************************************************
 * amplifychroma.h
 * vapoursynth-analog - analog-domain chroma gain filter
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 ******************************************************************************/

#ifndef AMPLIFYCHROMA_H
#define AMPLIFYCHROMA_H

#include <VapourSynth4.h>

void VS_CC CreateAmplifyChroma(const VSMap *In, VSMap *Out, void *userData,
                               VSCore *Core, const VSAPI *vsapi);

#endif
