#!/usr/bin/env bash
# Builds a Material3UI pacman package from this checkout.
#   build-pkg.sh NAME [--build DIR] [OUTDIR]
#       NAME is material3ui (the shell) or material3ui-greeter. Builds into
#       OUTDIR (default packaging/out) and prints the package's path.
#       --build packages a meson build dir already compiled (the shell).
#   build-pkg.sh --version
#       prints the version it would build
set -euo pipefail
ROOT="$(cd "$(dirname "$(readlink -f "${BASH_SOURCE[0]}")")/.." && pwd)"

# A release tag as is (a pre-release's hyphens dropped: v6.0-beta-1 is
# 6.0beta1, which pacman sorts before 6.0 and after 5.8); between releases
# tag.rN.gHASH, which sorts after it
version() {
    local desc
    desc="$(git -C "$ROOT" describe --tags --long --match 'v*' 2>/dev/null || true)"
    if [[ "$desc" =~ ^v(.+)-([0-9]+)-g([0-9a-f]+)$ ]]; then
        local v="${BASH_REMATCH[1]//-/}"
        [[ "${BASH_REMATCH[2]}" == 0 ]] || v+=".r${BASH_REMATCH[2]}.g${BASH_REMATCH[3]}"
        echo "$v"
    else
        tr -d '[:space:]-' <"$ROOT/VERSION"
    fi
}

if [[ "${1:-}" == --version ]]; then version; exit; fi
NAME="${1:?usage: build-pkg.sh NAME [--build DIR] [OUTDIR]}"
shift
[[ -f "$ROOT/packaging/$NAME/PKGBUILD" ]] || { echo "No package called $NAME" >&2; exit 2; }
BUILD=""
if [[ "${1:-}" == --build ]]; then
    BUILD="$(cd "${2:?--build needs a directory}" && pwd)"
    shift 2
    # A root `meson install` leaves its log root's; meson rewrites it
    rm -f "$BUILD/meson-logs/install-log.txt"
fi

OUT="$(mkdir -p "${1:-$ROOT/packaging/out}" && cd "${1:-$ROOT/packaging/out}" && pwd)"
VER="$(version)"
WORK="$(mktemp -d)"
trap 'rm -rf "$WORK"' EXIT
cp "$ROOT/packaging/$NAME/"* "$WORK/"
(cd "$WORK" && M3_SRC="$ROOT" M3_PKGVER="$VER" M3_BUILD="$BUILD" PKGDEST="$OUT" PKGEXT=.pkg.tar.zst \
    makepkg --force --nodeps --noconfirm >&2)
ls "$OUT/$NAME-$VER-1-"*.pkg.tar.zst | head -1
