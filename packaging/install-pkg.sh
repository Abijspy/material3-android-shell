#!/usr/bin/env bash
# Installs a Material3UI package with pacman (run as root):
#   install-pkg.sh FILE
# Files a source build put in place (meson install, the old greeter
# installer) are taken over by the package instead of stopping pacman.
set -euo pipefail
PKG="$(readlink -f "${1:?usage: install-pkg.sh FILE}")"
NAME="$(bsdtar -xOf "$PKG" .PKGINFO | sed -n 's/^pkgname = //p')"
OVERWRITE=()
if ! pacman -Qq "$NAME" >/dev/null 2>&1; then
    while IFS= read -r f; do
        [[ "$f" == .* || "$f" == */ ]] && continue
        OVERWRITE+=(--overwrite "/$f")
    done < <(bsdtar -tf "$PKG")
fi
pacman -U --noconfirm "${OVERWRITE[@]}" "$PKG"
