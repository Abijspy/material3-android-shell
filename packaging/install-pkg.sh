#!/usr/bin/env bash
# Installs a Material3UI package with pacman (run as root):
#   install-pkg.sh FILE
# Its own files are taken over from whatever put them in place (meson
# install, the old greeter installer) instead of stopping pacman: also
# under an older package, whose list lacks a newer build's files.
set -euo pipefail
PKG="$(readlink -f "${1:?usage: install-pkg.sh FILE}")"
OVERWRITE=()
while IFS= read -r f; do
    [[ "$f" == .* || "$f" == */ ]] && continue
    OVERWRITE+=(--overwrite "/$f")
done < <(bsdtar -tf "$PKG")
pacman -U --noconfirm "${OVERWRITE[@]}" "$PKG"
