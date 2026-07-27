#!/usr/bin/env bash
# Gates the version copies that cannot read VERSION.txt for themselves.
#
# meson.build reads VERSION.txt directly and src/version.h is generated from it,
# so neither can drift. A release tag is chosen by whoever pushes it, so that is
# checked here instead.
#
#   tools/check_version.sh           # check the in-tree copies agree
#   tools/check_version.sh v0.3.0    # also require a release tag to match
#
set -euo pipefail

root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
status=0

fail() {
    printf 'version drift: %s\n' "$1" >&2
    status=1
}

version="$(tr -d '[:space:]' < "$root/VERSION.txt")"
if [ -z "$version" ]; then
    printf 'VERSION.txt is empty\n' >&2
    exit 1
fi
# Not full PEP 440: enough to catch a stray "v" prefix or a truncated edit,
# while allowing the prerelease forms this project ships (0.3.0a1, 0.3.0rc1).
if ! printf '%s' "$version" \
    | grep -Eq '^[0-9]+\.[0-9]+\.[0-9]+((a|b|rc|\.post|\.dev)[0-9]+)?$'; then
    printf 'VERSION.txt is not a PEP 440 release version: %s\n' "$version" >&2
    exit 1
fi
printf 'VERSION.txt: %s\n' "$version"

# meson.build must keep deriving from VERSION.txt rather than reintroducing a
# literal, which would drift silently since nothing downstream would notice.
if ! grep -q "version : files('VERSION.txt')" "$root/meson.build"; then
    fail "meson.build no longer reads the version from VERSION.txt"
fi

# The hand-maintained header is gone; it must stay generated.
if [ -f "$root/src/version.h" ]; then
    fail "src/version.h is checked in again; it is generated from src/version.h.in"
fi

if [ "$#" -gt 0 ]; then
    tag="$1"
    if [ "${tag#v}" != "$version" ]; then
        fail "tag ${tag} does not match VERSION.txt (${version}); bump VERSION.txt or retag"
    else
        printf 'tag %s matches\n' "$tag"
    fi
fi

exit "$status"
