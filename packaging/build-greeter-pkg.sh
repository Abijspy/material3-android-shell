#!/usr/bin/env bash
# Builds the material3ui-greeter pacman package from this checkout.
#   build-greeter-pkg.sh [OUTDIR]   builds into OUTDIR (default packaging/out)
#                                   and prints the package's path
#   build-greeter-pkg.sh --version  prints the version it would build
set -euo pipefail
ROOT="$(cd "$(dirname "$(readlink -f "${BASH_SOURCE[0]}")")/.." && pwd)"

# A release tag as is; between releases tag.rN.gHASH, which sorts after it
version() {
    local desc
    desc="$(git -C "$ROOT" describe --tags --long --match 'v*' 2>/dev/null || true)"
    if [[ "$desc" =~ ^v(.+)-([0-9]+)-g([0-9a-f]+)$ ]]; then
        local v="${BASH_REMATCH[1]//-/_}"
        [[ "${BASH_REMATCH[2]}" == 0 ]] || v+=".r${BASH_REMATCH[2]}.g${BASH_REMATCH[3]}"
        echo "$v"
    else
        tr -d '[:space:]' <"$ROOT/VERSION"
    fi
}

if [[ "${1:-}" == --version ]]; then version; exit; fi

OUT="$(mkdir -p "${1:-$ROOT/packaging/out}" && cd "${1:-$ROOT/packaging/out}" && pwd)"
VER="$(version)"
WORK="$(mktemp -d)"
trap 'rm -rf "$WORK"' EXIT
cp "$ROOT/packaging/material3ui-greeter/"* "$WORK/"
(cd "$WORK" && M3_SRC="$ROOT" M3_PKGVER="$VER" PKGDEST="$OUT" PKGEXT=.pkg.tar.zst \
    makepkg --force --nodeps --noconfirm >&2)
echo "$OUT/material3ui-greeter-$VER-1-any.pkg.tar.zst"
