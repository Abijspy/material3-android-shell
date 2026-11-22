#!/usr/bin/env bash
# Installs Material3UI packages with pacman, in one go (run as root):
#   install-pkg.sh FILE…   (the shell and its apps together: they share a library)
# Its own files are taken over from whatever put them in place (meson
# install, the old greeter installer) instead of stopping pacman: also
# under an older package, whose list lacks a newer build's files.
set -euo pipefail
[[ $# -gt 0 ]] || { echo "usage: install-pkg.sh FILE…" >&2; exit 2; }
PKGS=()
OVERWRITE=()
for p in "$@"; do
    PKG="$(readlink -f "$p")"
    PKGS+=("$PKG")
    while IFS= read -r f; do
        [[ "$f" == .* || "$f" == */ ]] && continue
        OVERWRITE+=(--overwrite "/$f")
    done < <(bsdtar -tf "$PKG")
done
pacman -U --noconfirm "${OVERWRITE[@]}" "${PKGS[@]}"
