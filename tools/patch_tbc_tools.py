#!/usr/bin/env python3
"""Idempotently patch the tbc-tools submodule for vapoursynth-analog.

Three changes are required against harrypm/tbc-tools' ld-chroma-decoder:

1. Add a ``QString nnModelPath`` field to ``Comb::Configuration``. This lets us
   feed the ONNX model path through at runtime instead of relying on the
   embedded ``chroma_net_v2_onnx_data.h`` byte blob the upstream CMake
   generates.
2. Make the CPU/non-CUDA execution-provider path in ``Comb::FrameBuffer::
   split3DnnTransform`` load the ONNX session from ``configuration.nnModelPath``
   when set, falling back to the embedded blob only if the path is empty *and*
   the build defines ``VSANALOG_USE_EMBEDDED_CHROMA_NET_BLOB``. We don't
   generate the blob at all by default so the fallback would error out cleanly
   if reached.
3. Try the CoreML execution provider before falling back to CPU on macOS.

Each sub-patch checks for its own anchor and is independently idempotent;
re-running on already-patched files is a no-op.
"""
from __future__ import annotations

import sys
from pathlib import Path

PATCH_MARKER = "// vsanalog-nn-model-path-patch"
COREML_MARKER = "// vsanalog-coreml-patch"
PROVIDER_MARKER = "// vsanalog-nn-provider-patch"


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

    if changed:
        path.write_text(text)
    return changed


def patch_comb_cpp_model_path(text: str) -> tuple[str, bool]:
    if PATCH_MARKER in text:
        return text, False

    # 1) Make the embedded-blob include conditional.
    old_include = '#include "chroma_net_v2_onnx_data.h"'
    new_include = (
        "// " + PATCH_MARKER + " - embedded blob disabled by default\n"
        "#if defined(VSANALOG_USE_EMBEDDED_CHROMA_NET_BLOB)\n"
        + old_include
        + "\n#endif"
    )
    if old_include not in text:
        raise RuntimeError(
            f"comb.cpp: anchor for embedded blob include not found in {path}"
        )
    text = text.replace(old_include, new_include, 1)

    # 2) Allow the std::call_once lambda to access ``configuration``.
    old_callonce = "    std::call_once(onnxInitOnce, []() {"
    new_callonce = "    std::call_once(onnxInitOnce, [this]() {"
    if old_callonce not in text:
        raise RuntimeError(
            f"comb.cpp: anchor for onnx call_once lambda not found in {path}"
        )
    text = text.replace(old_callonce, new_callonce, 1)

    # 3) Replace the byte-blob session creation with a path-first fallback.
    old_session = (
        "                    ortSession = std::make_unique<Ort::Session>(\n"
        "                        *ortEnv,\n"
        "                        static_cast<const void *>(kChromaNetV2OnnxData),\n"
        "                        kChromaNetV2OnnxDataSize,\n"
        "                        options\n"
        "                    );"
    )
    new_session = (
        "                    // " + PATCH_MARKER + "\n"
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


def patch_comb_cpp_coreml(text: str) -> tuple[str, bool]:
    if COREML_MARKER in text:
        return text, False

    old_block = (
        "            if (!onnxReady) {\n"
        "                Ort::SessionOptions cpuOptions;\n"
        "                configureSessionOptions(cpuOptions);\n"
        "                tryCreateSession(cpuOptions, QStringLiteral(\"CPU\"));\n"
        "            }"
    )
    new_block = (
        "            // " + COREML_MARKER + "\n"
        "#if defined(__APPLE__)\n"
        "            if (!onnxReady) {\n"
        "                Ort::SessionOptions coremlOptions;\n"
        "                configureSessionOptions(coremlOptions);\n"
        "                try {\n"
        "                    coremlOptions.AppendExecutionProvider(\"CoreML\", {{\"MLComputeUnits\", \"ALL\"}, {\"ModelFormat\", \"MLProgram\"}});\n"
        "                    tryCreateSession(coremlOptions, QStringLiteral(\"CoreML\"));\n"
        "                } catch (const std::exception &vsCoreMLErr) {\n"
        "                    qWarning() << \"nnTransform3D CoreML EP unavailable:\"\n"
        "                               << vsCoreMLErr.what();\n"
        "                }\n"
        "            }\n"
        "#endif\n"
        "            if (!onnxReady) {\n"
        "                Ort::SessionOptions cpuOptions;\n"
        "                configureSessionOptions(cpuOptions);\n"
        "                tryCreateSession(cpuOptions, QStringLiteral(\"CPU\"));\n"
        "            }"
    )
    if old_block not in text:
        raise RuntimeError(
            "comb.cpp: anchor for CPU fallback block not found"
        )
    return text.replace(old_block, new_block, 1), True


def patch_comb_cpp_provider_override(text: str) -> tuple[str, bool]:
    """Allow ``configuration.nnProvider`` to override the env-var-derived
    provider preference. Also gate our CoreML attempt on the override."""
    if PROVIDER_MARKER in text:
        return text, False

    # 1) Drop the ``const`` qualifier off providerPreference and inject the
    #    override block immediately after it.
    old_pref = (
        "            const NnExecutionProviderPreference providerPreference "
        "= getNnExecutionProviderPreference();"
    )
    new_pref = (
        "            // " + PROVIDER_MARKER + "\n"
        "            NnExecutionProviderPreference providerPreference "
        "= getNnExecutionProviderPreference();\n"
        "            const QString vsCfgProvider = configuration.nnProvider.trimmed().toLower();\n"
        "            if (vsCfgProvider == \"cpu\") {\n"
        "                providerPreference = NnExecutionProviderPreference::Cpu;\n"
        "            } else if (vsCfgProvider == \"cuda\" || vsCfgProvider == \"gpu\") {\n"
        "                providerPreference = NnExecutionProviderPreference::Cuda;\n"
        "            }"
    )
    if old_pref not in text:
        raise RuntimeError(
            "comb.cpp: anchor for providerPreference declaration not found"
        )
    text = text.replace(old_pref, new_pref, 1)

    # 2) Gate the CoreML attempt on nn_provider != "cpu".
    old_coreml_gate = (
        "#if defined(__APPLE__)\n"
        "            if (!onnxReady) {\n"
        "                Ort::SessionOptions coremlOptions;"
    )
    new_coreml_gate = (
        "#if defined(__APPLE__)\n"
        "            if (!onnxReady && vsCfgProvider != \"cpu\") {\n"
        "                Ort::SessionOptions coremlOptions;"
    )
    if old_coreml_gate not in text:
        raise RuntimeError(
            "comb.cpp: anchor for CoreML gate not found (CoreML patch missing?)"
        )
    text = text.replace(old_coreml_gate, new_coreml_gate, 1)

    return text, True


def patch_comb_cpp(path: Path) -> bool:
    text = path.read_text()
    text, changed_mp = patch_comb_cpp_model_path(text)
    text, changed_cm = patch_comb_cpp_coreml(text)
    text, changed_pp = patch_comb_cpp_provider_override(text)
    if changed_mp or changed_cm or changed_pp:
        path.write_text(text)
    return changed_mp or changed_cm or changed_pp


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
