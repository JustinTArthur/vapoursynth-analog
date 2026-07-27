/******************************************************************************
 * chdlog.cpp
 * vapoursynth-analog - route libchromadec diagnostics into the VapourSynth log
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 ******************************************************************************/

#include "chdlog.h"

#include <algorithm>
#include <mutex>
#include <vector>

namespace {

struct Destination {
    VSCore *core;
    const VSAPI *vsapi;
};

std::mutex g_mutex;
// One entry per live attachment, so a core stays reachable while any of its
// filter instances is alive.
std::vector<Destination> g_destinations;

int toVSMessageType(chd_log_level_t level) {
    switch (level) {
        case CHD_LOG_DEBUG: return mtDebug;
        case CHD_LOG_INFO:  return mtInformation;
        case CHD_LOG_WARN:  return mtWarning;
        default:            return mtCritical;  // never mtFatal; it kills the host
    }
}

// Called from libchromadec's decode threads as well as ours. logMessage is
// thread-safe and doesn't re-enter libchromadec, and the destination is held
// under g_mutex for the whole call so detach() cannot retire a core underneath
// it. attach()/detach() drop g_mutex before touching libchromadec's own sink
// lock, so the two orderings can't close a cycle.
void sink(chd_log_level_t level, chd_log_flags_t flags, const char *message, void *) {
    // A flagged message is the detail of a failure the library is handing back
    // as a status, which we turn into a filter error or a creation error either
    // way, so logging it as well would report the same reason twice. Only
    // errors with no return path of their own carry no flag, and those are the
    // ones worth saying out loud.
    if ((flags & CHD_LOG_F_RETURNED) != 0) return;

    std::lock_guard<std::mutex> lock(g_mutex);
    if (g_destinations.empty()) return;
    // libchromadec's sink is process-global while a VapourSynth log is
    // per-core. With more than one core alive the newest attachment is the
    // best guess at who is decoding.
    const Destination &dest = g_destinations.back();
    dest.vsapi->logMessage(toVSMessageType(level), message, dest.core);
}

}  // namespace

void chdlog::attach(VSCore *core, const VSAPI *vsapi) {
    bool install;
    {
        std::lock_guard<std::mutex> lock(g_mutex);
        install = g_destinations.empty();
        g_destinations.push_back({core, vsapi});
    }
    if (install) chd_set_log_callback(sink, nullptr);
}

void chdlog::detach(VSCore *core) {
    bool uninstall;
    {
        std::lock_guard<std::mutex> lock(g_mutex);
        auto it = std::find_if(
            g_destinations.begin(), g_destinations.end(),
            [core](const Destination &d) { return d.core == core; });
        if (it == g_destinations.end()) return;
        g_destinations.erase(it);
        uninstall = g_destinations.empty();
    }
    // Returns only once the sink is neither running nor reachable.
    if (uninstall) chd_set_log_callback(nullptr, nullptr);
}

void chdlog::setLevel(chd_log_level_t level) {
    chd_set_log_level(level);
}
