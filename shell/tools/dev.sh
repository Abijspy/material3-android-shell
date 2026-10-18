#!/usr/bin/env bash
# Build, or run headless with a screenshot, inside the material3ui-dev container.
#   shell/tools/dev.sh build
#   shell/tools/dev.sh shot [out.png]   (env: M3_FAKE_HYPR=1, M3_SCRIPT, M3_WAIT)
set -euo pipefail
here="$(cd "$(dirname "$0")/.." && pwd)"
image=material3ui-dev
podman image exists "$image" || podman build -t "$image" -f "$here/tools/Containerfile" "$here/tools"

run() { podman run --rm -e M3_FAKE_HYPR -e M3_SCRIPT -e M3_WAIT -e M3_DEBUG_FRAMES -e M3_GDB -v "$here:/src:Z" -w /src "$image" "$@"; }

case "${1:-build}" in
build) run bash -c '[ -d build ] || meson setup build >/dev/null; meson compile -C build' ;;
shot) run tools/headless.sh "${2:-shot.png}" ;;
*) echo "usage: $0 build|shot [file]" >&2; exit 2 ;;
esac
