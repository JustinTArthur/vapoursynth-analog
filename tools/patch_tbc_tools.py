#!/usr/bin/env python3
"""Idempotently patch the tbc-tools submodule for vapoursynth-analog.

Two changes are required against harrypm/tbc-tools' ld-chroma-decoder:

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

Idempotent: re-running on already-patched files is a no-op.
"""
from __future__ import annotations

import sys
from pathlib import Path

PATCH_MARKER = "// vsanalog-nn-model-path-patch"


def patch_comb_h(path: Path) -> bool:
    text = path.read_text()
    if PATCH_MARKER in text:
        return False

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
    new_text = text.replace(needle, insertion + needle, 1)
    path.write_text(new_text)
    return True


def patch_comb_cpp(path: Path) -> bool:
    text = path.read_text()
    if PATCH_MARKER in text:
        return False

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
            f"comb.cpp: anchor for byte-blob session creation not found in {path}"
        )
    text = text.replace(old_session, new_session, 1)

    path.write_text(text)
    return True


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
