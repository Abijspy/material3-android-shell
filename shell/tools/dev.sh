#!/usr/bin/env bash
# Build, or run headless with a screenshot, inside the material3ui-dev container.
#   shell/tools/dev.sh build
#   shell/tools/dev.sh shot [out.png]   (env: M3_FAKE_HYPR=1, M3_SCRIPT, M3_WAIT)
set -euo pipefail
here="$(cd "$(dirname "$0")/.." && pwd)"
image=material3ui-dev
podman image exists "$image" || podman build -t "$image" -f "$here/tools/Containerfile" "$here/tools"

# The repo root is mounted read-only for shared data (emojis.txt…)
# gdk-pixbuf decodes in a bubblewrap sandbox (glycin), which needs seccomp and
# SELinux confinement relaxed inside this dev container
run() { podman run --rm --security-opt seccomp=unconfined --security-opt label=disable -v "$here/..:/repo:ro,Z" -e M3_DATA_DIR=/repo/dotfiles/.config/quickshell/material3ui/data -e M3_FAKE_HYPR -e M3_SCRIPT -e M3_WAIT -e M3_DEBUG_FRAMES -e M3_GDB -e M3_SERVICES -e M3_DEBUG_FOCUS -e M3_SCALE -e M3_SETUP -e M3_ARGS -e M3_USER -v "$here:/src:Z" -w /src "$image" "$@"; }

case "${1:-build}" in
build) run bash -c '[ -d build ] || meson setup build >/dev/null; meson compile -C build' ;;
shot) run tools/headless.sh "${2:-shot.png}" ;;
*) echo "usage: $0 build|shot [file]" >&2; exit 2 ;;
esac
