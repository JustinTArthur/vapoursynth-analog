/******************************************************************************
 * gpupreload.cpp
 * vapoursynth-analog - resolve pip-installed GPU vendor runtimes at load time
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 ******************************************************************************/

#include "gpupreload.h"

#include "chdlog.h"
#include "gpupreload_config.h"

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <cwctype>
#include <filesystem>
#include <mutex>
#include <string>
#include <string_view>
#include <vector>

#if defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#else
#include <dlfcn.h>
#endif

namespace {

struct PreloadEntry {
    const char *name;
    // Site-packages-relative directories, '/'-separated on every platform.
    std::vector<std::string> pipDirs;
};

const std::vector<PreloadEntry> kTable = { VSA_GPU_PRELOAD_TABLE };

// Families a vendor loader shim dlopens by bare soname at first use (cuDNN 9's
// engines). Nothing links them, so the wheel's startup preload skips them and
// they are resolved here instead, on the accelerated-backend path that is the
// only thing that needs them.
const std::vector<PreloadEntry> kDlopenTable = { VSA_GPU_DLOPEN_TABLE };

enum class Mode { Auto, System, Off };

Mode runtimeMode()
{
    const char *raw = std::getenv("VSANALOG_GPU_RUNTIME");
    if (raw == nullptr) return Mode::Auto;
    std::string v(raw);
    for (auto &c : v) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    if (v == "off" || v == "none" || v == "0" || v == "disabled") return Mode::Off;
    if (v == "system") return Mode::System;
    if (!v.empty() && v != "auto" && v != "pip") {
        chdlog::warn("VSANALOG_GPU_RUNTIME=" + v +
                     " is not recognised (expected auto, system or off); using auto");
    }
    return Mode::Auto;
}

// True when a library with this name is already mapped into the process —
// via the plugin's own link-time dependencies, the Python-side preload, or a
// co-resident consumer like torch. Loading a second copy by path would give
// the process two libraries with one soname, so anything resident always
// wins, whatever resolution mode is in effect.
bool alreadyResident(const char *name)
{
#if defined(_WIN32)
    return GetModuleHandleA(name) != nullptr;
#else
    return dlopen(name, RTLD_LAZY | RTLD_NOLOAD) != nullptr;
#endif
}

// The libraries are pinned for the lifetime of the process, so every
// successful load handle is dropped on purpose.
bool loadByPath(const std::filesystem::path &path, std::string *outError)
{
#if defined(_WIN32)
    // Altered search so a library's own dependent DLLs resolve next to it
    // first, matching the pip package layout.
    HMODULE mod = LoadLibraryExW(path.c_str(), nullptr, LOAD_WITH_ALTERED_SEARCH_PATH);
    if (mod != nullptr) return true;
    if (outError) *outError = "error " + std::to_string(GetLastError());
    return false;
#else
    dlerror();
    if (dlopen(path.c_str(), RTLD_NOW | RTLD_GLOBAL) != nullptr) return true;
    const char *msg = dlerror();
    if (outError) *outError = msg != nullptr ? msg : "dlopen failed";
    return false;
#endif
}

// Put a vendor directory on the process PATH, Windows only.
//
// LOAD_WITH_ALTERED_SEARCH_PATH above only covers a library's own link-time
// dependencies. TensorRT loads nvinfer_builder_resource_sm<arch>_10.dll — the
// per-architecture kernel resource its builder needs, ~170 MB, one of eight,
// so not preloadable from the table — with a plain LoadLibrary by bare name
// when it first builds an engine. That search reads PATH, which is how a
// normal TensorRT install advertises its lib directory; without it engine
// building fails with "Unable to load library:
// nvinfer_builder_resource_sm86_10.dll: 126".
void addToSearchPath(const std::filesystem::path &dir)
{
#if defined(_WIN32)
    if (dir.empty()) return;
    const std::wstring wanted = dir.wstring();
    std::wstring current;
    if (const DWORD len = GetEnvironmentVariableW(L"PATH", nullptr, 0); len > 0) {
        current.resize(len);
        const DWORD got = GetEnvironmentVariableW(L"PATH", current.data(), len);
        current.resize(got);
    }
    auto icaseEqual = [](std::wstring_view a, std::wstring_view b) {
        return a.size() == b.size() &&
               std::equal(a.begin(), a.end(), b.begin(), [](wchar_t x, wchar_t y) {
                   return towlower(x) == towlower(y);
               });
    };
    for (size_t start = 0; start <= current.size();) {
        const size_t end = current.find(L';', start);
        const size_t stop = end == std::wstring::npos ? current.size() : end;
        if (icaseEqual(std::wstring_view(current).substr(start, stop - start), wanted)) {
            return;
        }
        if (end == std::wstring::npos) break;
        start = end + 1;
    }
    const std::wstring updated =
        current.empty() ? wanted : wanted + L';' + current;
    SetEnvironmentVariableW(L"PATH", updated.c_str());
#else
    (void)dir;  // POSIX resolves the siblings through the loaded library itself.
#endif
}

bool loadByName(const char *name)
{
#if defined(_WIN32)
    return LoadLibraryA(name) != nullptr;
#else
    return dlopen(name, RTLD_NOW | RTLD_GLOBAL) != nullptr;
#endif
}

// Path of this plugin library, for anchoring site-packages without any
// Python involved: the wheel installs the plugin at
// <site-packages>/vapoursynth/plugins/vsanalog/, so site-packages is four
// parents up. Outside a wheel install the walk lands somewhere without a
// nvidia/ or tensorrt_libs/ subdirectory and the pip step naturally finds
// nothing.
std::filesystem::path pluginPath()
{
#if defined(_WIN32)
    HMODULE mod = nullptr;
    if (GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS |
                               GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                           reinterpret_cast<LPCWSTR>(&pluginPath), &mod) == 0) {
        return {};
    }
    std::wstring buf(4096, L'\0');
    const DWORD len = GetModuleFileNameW(mod, buf.data(), static_cast<DWORD>(buf.size()));
    if (len == 0 || len >= buf.size()) return {};
    buf.resize(len);
    return std::filesystem::path(buf);
#else
    Dl_info info{};
    if (dladdr(reinterpret_cast<void *>(&pluginPath), &info) == 0 ||
        info.dli_fname == nullptr) {
        return {};
    }
    return std::filesystem::path(info.dli_fname);
#endif
}

std::filesystem::path sitePackagesRoot()
{
    std::filesystem::path p = pluginPath();
    if (p.empty()) return {};
    for (int i = 0; i < 4; ++i) p = p.parent_path();
    return p;
}

void loadTable(const std::vector<PreloadEntry> &table,
               const std::filesystem::path &site,
               std::string &missing)
{
    for (const auto &entry : table) {
        if (alreadyResident(entry.name)) {
            chdlog::debug(std::string("GPU runtime preload: ") + entry.name +
                          " already loaded");
            continue;
        }

        bool loaded = false;
        if (!site.empty()) {
            for (const auto &dir : entry.pipDirs) {
                std::filesystem::path candidate = site;
                for (const auto &part : std::filesystem::path(dir)) candidate /= part;
                candidate /= entry.name;
                std::error_code ec;
                if (!std::filesystem::exists(candidate, ec)) continue;

                std::string loadError;
                if (loadByPath(candidate, &loadError)) {
                    chdlog::debug("GPU runtime preload: pinned " +
                                  candidate.string());
                    addToSearchPath(candidate.parent_path());
                    loaded = true;
                } else {
                    // A present-but-unloadable pip copy is worth saying out
                    // loud before the fallback hides it.
                    chdlog::info("GPU runtime preload: " + candidate.string() +
                                 " exists but failed to load (" + loadError +
                                 "); trying the system search paths");
                }
                break;
            }
        }

        if (!loaded && loadByName(entry.name)) {
            // Name lookup: LD_LIBRARY_PATH/ldconfig/PATH, or the wheel's own
            // embedded search-path entries when the system has no copy.
            chdlog::debug(std::string("GPU runtime preload: resolved ") +
                          entry.name + " by name lookup");
            loaded = true;
        }

        if (!loaded) {
            if (!missing.empty()) missing += ", ";
            missing += entry.name;
        }
    }
}

void preloadAll()
{
    if (kTable.empty() && kDlopenTable.empty()) return;

    const Mode mode = runtimeMode();
    if (mode == Mode::Off) {
        chdlog::debug("GPU runtime preload disabled by VSANALOG_GPU_RUNTIME");
        return;
    }

    std::filesystem::path site;
    if (mode != Mode::System) site = sitePackagesRoot();

    std::string missing;
    loadTable(kTable, site, missing);
    loadTable(kDlopenTable, site, missing);

    if (!missing.empty()) {
        chdlog::info(
            "GPU runtime libraries not found: " + missing +
            ". Execution providers that need them will be unavailable and the "
            "provider chain will fall through. Install the matching nvidia-* "
            "/ tensorrt pip packages in this environment, or a system runtime "
            "(then LD_LIBRARY_PATH/PATH resolution applies as before).");
    }
}

}  // namespace

void gpupreload::ensureLoaded()
{
    static std::once_flag once;
    std::call_once(once, preloadAll);
}
