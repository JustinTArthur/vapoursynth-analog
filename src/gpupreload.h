/******************************************************************************
 * gpupreload.h
 * vapoursynth-analog - resolve pip-installed GPU vendor runtimes at load time
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 ******************************************************************************/

#ifndef GPUPRELOAD_H
#define GPUPRELOAD_H

namespace gpupreload {

// Load the vendor GPU runtime libraries (CUDA, cuDNN, TensorRT) this wheel
// variant's ONNX Runtime execution providers dlopen at session creation, so
// they resolve from pip-installed packages (site-packages/nvidia/*,
// tensorrt_libs) without the user wiring LD_LIBRARY_PATH or PATH. Per
// library, in order: skip if already loaded in this process, load the pip
// copy by full path, fall back to an ordinary name lookup (which honours
// LD_LIBRARY_PATH, ldconfig and PATH exactly as before). Libraries that
// resolve nowhere are reported through the log sink and left to the
// provider-attach chain's own fallback.
//
// VSANALOG_GPU_RUNTIME=system skips the pip step; =off skips the preload
// entirely. Never touches the ONNX Runtime provider libraries themselves:
// loading those outside ORT's own sequence crashes on Windows (DllMain runs
// before ORT's host function table exists) and false-negatives on Linux
// (their Provider_GetHost import is satisfied by ORT, not the loader).
//
// Idempotent and thread-safe; a no-op for wheel variants with no vendor
// runtime (CPU, CoreML, DirectML).
void ensureLoaded();

}  // namespace gpupreload

#endif  // GPUPRELOAD_H
