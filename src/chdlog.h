/******************************************************************************
 * chdlog.h
 * vapoursynth-analog - route libchromadec diagnostics into the VapourSynth log
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 ******************************************************************************/

#ifndef CHDLOG_H
#define CHDLOG_H

#include <chromadec/log.h>

#include <VapourSynth4.h>

namespace chdlog {

// libchromadec emits nothing until a consumer installs a sink; failures come
// back as status codes but diagnostics have no return path, so without one the
// decoder's warnings (an NN backend falling back to 2D, a SECAM field ident
// that disagrees with the metadata) are lost. Route them to the core's log,
// where a script can pick them up with core.add_log_handler(). Failures the
// library reports back to us are left off the log, since they reach the script
// as errors already.
//
// Attach for as long as a filter instance is alive; the sink is installed on
// the first attachment and uninstalled on the last, so nothing can call into a
// core that is on its way out.
void attach(VSCore *core, const VSAPI *vsapi);
void detach(VSCore *core);

// Drop diagnostics below this level inside libchromadec. Process-global, like
// the sink itself; defaults to CHD_LOG_INFO.
void setLevel(chd_log_level_t level);

}  // namespace chdlog

#endif  // CHDLOG_H
