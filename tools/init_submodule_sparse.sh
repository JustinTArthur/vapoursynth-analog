#!/usr/bin/env bash
# Initialize the tbc-tools submodule with shallow history, partial clone, and
# sparse-checkout that excludes paths we don't compile against.
#
# Why bother:
#   - The harrypm/tbc-tools fork commits a Windows vcpkg binary cache under
#     ci/cache/ that's 5+ GB and growing. A vanilla `git submodule update
#     --init --recursive` pulls all of it.
#   - --depth=1 + --filter=blob:none avoids fetching history and blobs we
#     don't need; sparse-checkout then keeps blobs under excluded paths
#     from being lazy-fetched on demand.
#
# Run this from the vapoursynth-analog repo root after a fresh clone, and
# from CI in place of `submodules: recursive` in actions/checkout.

set -euo pipefail

SUBMODULE="extern/tbc-tools"

# Shallow + partial clone for the submodule. Honored by `git submodule update`.
git -c "submodule.${SUBMODULE}.shallow=true" \
    -c "submodule.${SUBMODULE}.fetchRecurseSubmodules=no" \
    -c "protocol.version=2" \
    submodule update --init --depth 1 --filter=blob:none --recommend-shallow \
    "${SUBMODULE}"

# Restrict the working tree to just the source code our build needs.
# /src/ holds ld-chroma-decoder + library/tbc that meson compiles. /LICENSE
# travels with the source per GPL.
git -C "${SUBMODULE}" sparse-checkout init --no-cone
git -C "${SUBMODULE}" sparse-checkout set '/src/' '/LICENSE'

echo "Submodule ${SUBMODULE} initialized:"
echo "  HEAD: $(git -C "${SUBMODULE}" rev-parse HEAD)"
echo "  Size: $(du -sh "${SUBMODULE}" 2>/dev/null | cut -f1 || echo '?')"