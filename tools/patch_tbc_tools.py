#!/usr/bin/env python3
"""Idempotently patch the tbc-tools submodule for vapoursynth-analog.

Five changes are required against harrypm/tbc-tools' ld-chroma-decoder:

1. Add ``QString nnModelPath``, ``QString nnProvider``, and
   ``double nnInputMagnitudeScale`` fields to ``Comb::Configuration``. The
   model path lets us feed the ONNX file through at runtime instead of
   relying on the embedded ``chroma_net_v2_onnx_data.h`` byte blob the
   upstream CMake generates; ``nnProvider`` is a per-instance override for
   the execution provider that would otherwise come from the
   ``LDDECODE_NNTRANSFORM3D_PROVIDER`` env var; ``nnInputMagnitudeScale``
   is the divisor the loaded model's training contract expects on its
   input magnitude spectrum.
2. In ``Comb::FrameBuffer::split3DnnTransform``, honor those fields:
   load the session from ``configuration.nnModelPath`` when set (falling
   back to the embedded blob only if compiled with
   ``VSANALOG_USE_EMBEDDED_CHROMA_NET_BLOB``), and map
   ``configuration.nnProvider`` onto upstream's
   ``NnExecutionProviderPreference`` enum (``cpu``/``cuda``/``gpu``/``coreml``).
3. Reload the static ONNX session when ``configuration.nnModelPath``
   changes. Upstream uses ``std::call_once`` to initialize a single
   process-wide session, which made sense when the only weights source
   was the embedded blob. With runtime-supplied model paths, two
   ``nntransform3d`` filter instances pointing at different ``.onnx``
   files would otherwise share whichever session loaded first.
4. Divide the model's input magnitude spectrum by
   ``configuration.nnInputMagnitudeScale``. The nnTransform3D v2 weights
   were trained on inputs scaled down by 128; feeding them unscaled
   magnitudes gives the network out-of-distribution input.
5. Stop ``ensureWindowsOnnxCudaProviderLoaded``'s probe from cold-loading
   ORT's provider DLLs. The probe ``LoadLibrary``s
   ``onnxruntime_providers_cuda.dll`` to test for CUDA; that runs the
   provider's ``DllMain`` static initializers before ORT's provider host
   is set, so the provider's overridden ``operator new`` dereferences a
   null host and the load faults with ``ERROR_DLL_INIT_FAILED``. The probe
   then wrongly reports CUDA unavailable and the decoder silently falls
   back to CPU. ORT loads the provider correctly itself when the CUDA EP
   is appended, so the probe must not pre-empt it.

Each sub-patch checks for its own anchor and is independently idempotent;
re-running on already-patched files is a no-op.
"""
from __future__ import annotations

import sys
from pathlib import Path

PATCH_MARKER = "// vsanalog-nn-model-path-patch"
PROVIDER_MARKER = "// vsanalog-nn-provider-patch"
RELOAD_MARKER = "// vsanalog-nn-session-reload-patch"
SCALE_MARKER = "// vsanalog-nn-input-scale-patch"
COLDLOAD_MARKER = "// vsanalog-nn-provider-coldload-patch"


def patch_comb_h(path: Path) -> bool:
    text = path.read_text()
    changed = False
    if PATCH_MARKER not in text:
        # Insert ``QString nnModelPath`` into Comb::Configuration.
        needle = "        qint32 getLookBehind() const;"
        insertion = (
            "        " + PATCH_MARKER + "\n"
            "        // Filesystem path to the ONNX model file used when\n"
            "        // ``nnTransform3D`` is enabled. When empty, the build's\n"
            "        // embedded byte blob is used (only available when compiled\n"
            "        // with ``VSANALOG_USE_EMBEDDED_CHROMA_NET_BLOB``).\n"
            "        QString nnModelPath;\n\n"
        )
        if needle not in text:
            raise RuntimeError(
                f"comb.h: anchor for nnModelPath insertion not found in {path}"
            )
        text = text.replace(needle, insertion + needle, 1)
        changed = True

    if PROVIDER_MARKER not in text:
        # Insert ``QString nnProvider`` alongside nnModelPath.
        needle = "        QString nnModelPath;"
        insertion = (
            "        QString nnModelPath;\n\n"
            "        " + PROVIDER_MARKER + "\n"
            "        // Override for the ONNX execution provider. Empty means\n"
            "        // honor the LDDECODE_NNTRANSFORM3D_PROVIDER env var; values\n"
            "        // are matched case-insensitively against \"auto\", \"cpu\",\n"
            "        // \"cuda\"/\"gpu\", \"coreml\".\n"
            "        QString nnProvider;"
        )
        if needle not in text:
            raise RuntimeError(
                f"comb.h: anchor for nnProvider insertion not found in {path}"
            )
        text = text.replace(needle, insertion, 1)
        changed = True

    if SCALE_MARKER not in text:
        # Insert ``double nnInputMagnitudeScale`` alongside nnProvider.
        needle = "        QString nnProvider;"
        insertion = (
            "        QString nnProvider;\n\n"
            "        " + SCALE_MARKER + "\n"
            "        // Divisor applied to the model's input magnitude\n"
            "        // spectrum. nnTransform3D v2 was trained on inputs\n"
            "        // scaled down by 128; v1 expects raw magnitudes (1.0).\n"
            "        double nnInputMagnitudeScale = 1.0;"
        )
        if needle not in text:
            raise RuntimeError(
                f"comb.h: anchor for nnInputMagnitudeScale insertion not "
                f"found in {path}"
            )
        text = text.replace(needle, insertion, 1)
        changed = True

    if changed:
        path.write_text(text)
    return changed


def patch_comb_cpp_model_path(text: str) -> tuple[str, bool]:
    if PATCH_MARKER in text:
        return text, False

    # 1) Make the embedded-blob include conditional.
    old_include = '#include "chroma_net_v2_onnx_data.h"'
    new_include = (
        PATCH_MARKER + " - embedded blob disabled by default\n"
        "#if defined(VSANALOG_USE_EMBEDDED_CHROMA_NET_BLOB)\n"
        + old_include
        + "\n#endif"
    )
    if old_include not in text:
        raise RuntimeError(
            "comb.cpp: anchor for embedded blob include not found"
        )
    text = text.replace(old_include, new_include, 1)

    # 2) Allow the std::call_once lambda to access ``configuration``. The
    #    inner ``tryCreateSession`` lambda uses ``[&]`` so it inherits.
    old_callonce = "    std::call_once(onnxInitOnce, []() {"
    new_callonce = "    std::call_once(onnxInitOnce, [this]() {"
    if old_callonce not in text:
        raise RuntimeError(
            "comb.cpp: anchor for onnx call_once lambda not found"
        )
    text = text.replace(old_callonce, new_callonce, 1)

    # 3) Replace the byte-blob session creation (inside the
    #    ``tryCreateSession`` lambda) with a path-first / blob-fallback
    #    dispatch.
    old_session = (
        "                    ortSession = std::make_unique<Ort::Session>(\n"
        "                        *ortEnv,\n"
        "                        static_cast<const void *>(kChromaNetV2OnnxData),\n"
        "                        kChromaNetV2OnnxDataSize,\n"
        "                        options\n"
        "                    );"
    )
    new_session = (
        "                    " + PATCH_MARKER + "\n"
        "                    if (!configuration.nnModelPath.isEmpty()) {\n"
        "#ifdef _WIN32\n"
        "                        const std::wstring vsModelPath =\n"
        "                            configuration.nnModelPath.toStdWString();\n"
        "#else\n"
        "                        const QByteArray vsModelPathBytes =\n"
        "                            QFile::encodeName(configuration.nnModelPath);\n"
        "                        const std::string vsModelPath(\n"
        "                            vsModelPathBytes.constData(),\n"
        "                            static_cast<size_t>(vsModelPathBytes.size())\n"
        "                        );\n"
        "#endif\n"
        "                        ortSession = std::make_unique<Ort::Session>(\n"
        "                            *ortEnv,\n"
        "                            vsModelPath.c_str(),\n"
        "                            options\n"
        "                        );\n"
        "                    } else {\n"
        "#if defined(VSANALOG_USE_EMBEDDED_CHROMA_NET_BLOB)\n"
        "                        ortSession = std::make_unique<Ort::Session>(\n"
        "                            *ortEnv,\n"
        "                            static_cast<const void *>(kChromaNetV2OnnxData),\n"
        "                            kChromaNetV2OnnxDataSize,\n"
        "                            options\n"
        "                        );\n"
        "#else\n"
        "                        qWarning() << \"nnTransform3D model path is empty\"\n"
        "                                   << \"and no embedded blob is available\";\n"
        "                        return false;\n"
        "#endif\n"
        "                    }"
    )
    if old_session not in text:
        raise RuntimeError(
            "comb.cpp: anchor for byte-blob session creation not found"
        )
    text = text.replace(old_session, new_session, 1)

    return text, True


def patch_comb_cpp_provider_override(text: str) -> tuple[str, bool]:
    """Map ``configuration.nnProvider`` onto upstream's
    ``NnExecutionProviderPreference`` enum, overriding the env-var-derived
    preference when non-empty."""
    if PROVIDER_MARKER in text:
        return text, False

    # Drop the ``const`` qualifier off providerPreference and inject the
    # override block immediately after it.
    old_pref = (
        "            const NnExecutionProviderPreference providerPreference "
        "= getNnExecutionProviderPreference();"
    )
    new_pref = (
        "            " + PROVIDER_MARKER + "\n"
        "            NnExecutionProviderPreference providerPreference "
        "= getNnExecutionProviderPreference();\n"
        "            const QString vsCfgProvider = configuration.nnProvider.trimmed().toLower();\n"
        "            if (vsCfgProvider == \"cpu\") {\n"
        "                providerPreference = NnExecutionProviderPreference::Cpu;\n"
        "            } else if (vsCfgProvider == \"cuda\" || vsCfgProvider == \"gpu\") {\n"
        "                providerPreference = NnExecutionProviderPreference::Cuda;\n"
        "            } else if (vsCfgProvider == \"coreml\") {\n"
        "                providerPreference = NnExecutionProviderPreference::CoreML;\n"
        "            }"
    )
    if old_pref not in text:
        raise RuntimeError(
            "comb.cpp: anchor for providerPreference declaration not found"
        )
    return text.replace(old_pref, new_pref, 1), True


def patch_comb_cpp_session_reload(text: str) -> tuple[str, bool]:
    """Make the static ONNX session reload when ``configuration.nnModelPath``
    changes from one ``split3DnnTransform`` call to the next, instead of
    locking the first-loaded model in for the process lifetime.

    Strategy: replace ``std::once_flag`` with a unique_ptr that can be
    re-created on path change, and gate the reset on a separate mutex
    that also blocks during in-flight inference so we don't free a
    session another thread is mid-Run on.
    """
    if RELOAD_MARKER in text:
        return text, False

    # 1) Promote onnxInitOnce to a unique_ptr so it can be replaced when
    #    the requested model path changes.
    old_once_decl = "    static std::once_flag onnxInitOnce;"
    new_once_decl = (
        "    " + RELOAD_MARKER + "\n"
        "    static std::unique_ptr<std::once_flag> onnxInitOnce "
        "= std::make_unique<std::once_flag>();"
    )
    if old_once_decl not in text:
        raise RuntimeError(
            "comb.cpp: anchor for onnxInitOnce declaration not found"
        )
    text = text.replace(old_once_decl, new_once_decl, 1)

    # 2) Insert a path-tracking reset block immediately before the
    #    std::call_once invocation, and dereference the unique_ptr in
    #    the invocation itself. ``patch_comb_cpp_model_path`` runs before
    #    this and rewrites the lambda capture to ``[this]``, so we
    #    anchor on the post-model-path-patch form.
    old_call = "    std::call_once(onnxInitOnce, [this]() {"
    new_call = (
        "    static QString onnxLoadedModelPath;\n"
        "    static std::mutex onnxResetMutex;\n"
        "    {\n"
        "        std::lock_guard<std::mutex> resetLock(onnxResetMutex);\n"
        "        if (onnxLoadedModelPath != configuration.nnModelPath) {\n"
        "            std::lock_guard<std::mutex> runLock(onnxRunMutex);\n"
        "            onnxInitOnce = std::make_unique<std::once_flag>();\n"
        "            ortSession.reset();\n"
        "            onnxReady = false;\n"
        "            onnxLoadedModelPath = configuration.nnModelPath;\n"
        "        }\n"
        "    }\n"
        "    std::call_once(*onnxInitOnce, [this]() {"
    )
    if old_call not in text:
        raise RuntimeError(
            "comb.cpp: anchor for std::call_once invocation not found"
        )
    text = text.replace(old_call, new_call, 1)

    return text, True


def patch_comb_cpp_input_scale(text: str) -> tuple[str, bool]:
    """Divide the model's input magnitude spectrum by
    ``configuration.nnInputMagnitudeScale``. The reflected-magnitude channel
    is built by indexing back into the same ``magnitudes`` buffer, so scaling
    it once at computation covers both input channels."""
    if SCALE_MARKER in text:
        return text, False

    old_loop = (
        "            for (qint32 i = 0; i < Nt * Ny * Nx; i++) {\n"
        "                magnitudes[i] = sqrtf(static_cast<float>("
        "(out[i][0] * out[i][0]) + (out[i][1] * out[i][1])));\n"
        "            }"
    )
    new_loop = (
        "            " + SCALE_MARKER + "\n"
        "            const float vsMagScale = (configuration.nnInputMagnitudeScale > 0.0)\n"
        "                ? static_cast<float>(1.0 / configuration.nnInputMagnitudeScale)\n"
        "                : 1.0f;\n"
        "            for (qint32 i = 0; i < Nt * Ny * Nx; i++) {\n"
        "                magnitudes[i] = sqrtf(static_cast<float>("
        "(out[i][0] * out[i][0]) + (out[i][1] * out[i][1]))) * vsMagScale;\n"
        "            }"
    )
    if old_loop not in text:
        raise RuntimeError(
            "comb.cpp: anchor for the magnitude-computation loop not found"
        )
    return text.replace(old_loop, new_loop, 1), True


def patch_comb_cpp_provider_coldload(text: str) -> tuple[str, bool]:
    """Stop the Windows CUDA-provider probe from cold-loading ORT's
    provider DLLs.

    ``ensureWindowsOnnxCudaProviderLoaded`` probes for CUDA support by
    calling ``LoadLibraryW`` on ``onnxruntime_providers_shared.dll`` and
    ``onnxruntime_providers_cuda.dll``. That is fatal: ONNX Runtime's
    provider DLLs override ``operator new`` to route allocations through a
    provider host pointer that is only set when ORT loads the provider
    itself (via ``ProviderLibrary::Load``, which initializes
    ``providers_shared`` and performs the host handshake first). A bare
    ``LoadLibrary`` runs ``providers_cuda``'s ``DllMain`` static
    initializers — e.g. the file-scope ``ort_triton_kernel_group_map``
    ``std::unordered_map`` in ``triton_kernel.cu`` — before the host
    exists; ``operator new`` then dereferences a null host pointer and the
    load faults with ``ERROR_DLL_INIT_FAILED``. The probe reports CUDA as
    unavailable and the decoder silently falls back to CPU.

    Fix: neuter the ``tryLoadLibrary`` lambda so it never calls
    ``LoadLibrary`` and always reports success. The probe then no longer
    pre-empts ORT, which loads the provider the correct way when the CUDA
    EP is appended (``SessionOptionsAppendExecutionProvider_CUDA_V2``) and
    reports a clean status if it is genuinely unavailable.
    """
    if COLDLOAD_MARKER in text:
        return text, False

    old_try = (
        "        auto tryLoadLibrary = [&](const QString &libraryPath, "
        "QString &loadError) -> bool {\n"
        "            HMODULE module = LoadLibraryW("
        "reinterpret_cast<LPCWSTR>(libraryPath.utf16()));\n"
        "            if (module != nullptr) {\n"
        "                return true;\n"
        "            }\n"
        "\n"
        "            loadError = formatWindowsError(GetLastError());\n"
        "            return false;\n"
        "        };"
    )
    new_try = (
        "        " + COLDLOAD_MARKER + "\n"
        "        // Never LoadLibrary an ONNX Runtime provider DLL. Cold-loading\n"
        "        // onnxruntime_providers_cuda.dll runs its DllMain static\n"
        "        // initializers (e.g. the ort_triton_kernel_group_map global)\n"
        "        // before ORT's provider host is set; its overridden operator\n"
        "        // new then dereferences a null host and the load faults with\n"
        "        // ERROR_DLL_INIT_FAILED. ORT loads the provider correctly\n"
        "        // itself when the CUDA EP is appended; this probe must not\n"
        "        // pre-empt it, so report every candidate as loadable.\n"
        "        auto tryLoadLibrary = [&](const QString &libraryPath, "
        "QString &loadError) -> bool {\n"
        "            Q_UNUSED(libraryPath);\n"
        "            Q_UNUSED(loadError);\n"
        "            Q_UNUSED(formatWindowsError);\n"
        "            return true;\n"
        "        };"
    )
    if old_try not in text:
        raise RuntimeError(
            "comb.cpp: anchor for the tryLoadLibrary lambda not found"
        )
    return text.replace(old_try, new_try, 1), True


def patch_comb_cpp(path: Path) -> bool:
    text = path.read_text()
    text, changed_mp = patch_comb_cpp_model_path(text)
    text, changed_pp = patch_comb_cpp_provider_override(text)
    text, changed_sr = patch_comb_cpp_session_reload(text)
    text, changed_is = patch_comb_cpp_input_scale(text)
    text, changed_cl = patch_comb_cpp_provider_coldload(text)
    if changed_mp or changed_pp or changed_sr or changed_is or changed_cl:
        path.write_text(text)
    return changed_mp or changed_pp or changed_sr or changed_is or changed_cl


def main(argv: list[str]) -> int:
    if len(argv) != 2:
        sys.stderr.write(
            "usage: patch_tbc_tools.py <path-to-tbc-tools-checkout>\n"
        )
        return 2

    root = Path(argv[1])
    chroma_dir = root / "src" / "ld-chroma-decoder"
    comb_h = chroma_dir / "comb.h"
    comb_cpp = chroma_dir / "comb.cpp"

    if not comb_h.is_file() or not comb_cpp.is_file():
        sys.stderr.write(
            f"patch_tbc_tools.py: comb.h/comb.cpp not found under {chroma_dir}\n"
        )
        return 1

    h_changed = patch_comb_h(comb_h)
    cpp_changed = patch_comb_cpp(comb_cpp)

    print(
        f"patch_tbc_tools.py: comb.h "
        f"{'patched' if h_changed else 'already patched'}; "
        f"comb.cpp {'patched' if cpp_changed else 'already patched'}"
    )
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))