#!/usr/bin/env bash
# Patch the Homebrew onnxruntime install so it follows the SOVERSION dylib
# conventions the rest of our brew-installed deps (fftw, sqlite, etc.) follow.
#
# Why: brew ships libonnxruntime.X.Y.Z.dylib with install_name pinned to that
# exact filename, and no libonnxruntime.1.dylib symlink. A naive link bakes the
# patch version into our binary, and `delocate-wheel` can't resolve a
# `@rpath/libonnxruntime.1.dylib` reference because that filename doesn't
# exist on the brew tree. Both problems vanish if we (1) override the
# install_name to @rpath/libonnxruntime.1.dylib on the actual file and
# (2) add the missing .1 symlink. Idempotent.
#
# Run from CI on macOS runners after `brew install onnxruntime`.
set -euo pipefail

PREFIX=$(brew --prefix onnxruntime)
LIBDIR="${PREFIX}/lib"

REAL=$(ls "${LIBDIR}"/libonnxruntime.1.*.dylib 2>/dev/null | head -1)
if [ -z "${REAL}" ]; then
    echo "patch_brew_onnxruntime: could not find libonnxruntime.1.*.dylib in ${LIBDIR}" >&2
    exit 1
fi

# install_name → @rpath/libonnxruntime.1.dylib (no-op if already set).
current_id=$(otool -D "${REAL}" | tail -1)
target_id="@rpath/libonnxruntime.1.dylib"
if [ "${current_id}" != "${target_id}" ]; then
    sudo install_name_tool -id "${target_id}" "${REAL}"
    # Re-sign after install_name_tool invalidates the ad-hoc signature.
    sudo codesign --force --sign - "${REAL}" 2>/dev/null || true
    echo "patch_brew_onnxruntime: install_name -> ${target_id}"
else
    echo "patch_brew_onnxruntime: install_name already ${target_id}"
fi

# .1.dylib symlink → real file (idempotent).
ln_target=$(basename "${REAL}")
ln_path="${LIBDIR}/libonnxruntime.1.dylib"
if [ ! -L "${ln_path}" ] || [ "$(readlink "${ln_path}")" != "${ln_target}" ]; then
    sudo ln -sf "${ln_target}" "${ln_path}"
    echo "patch_brew_onnxruntime: ${ln_path} -> ${ln_target}"
else
    echo "patch_brew_onnxruntime: ${ln_path} symlink already correct"
fi