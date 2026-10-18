#!/usr/bin/env bash
# Runs inside the dev container: headless Sway, optionally a fake Hyprland,
# the shell, an optional command script, then a screenshot.
#   headless.sh <out.png>   env: M3_FAKE_HYPR=1, M3_SCRIPT="…", M3_WAIT=seconds
set -u
out="${1:-shot.png}"
[ -d build ] || meson setup build >/dev/null
meson compile -C build >/dev/null || exit 1

export XDG_RUNTIME_DIR=/tmp/xdg
mkdir -p -m 700 "$XDG_RUNTIME_DIR"
export WLR_BACKENDS=headless WLR_RENDERER=pixman WLR_LIBINPUT_NO_DEVICES=1

# sway carries file capabilities, which a rootless container refuses to exec
cp /usr/bin/sway /tmp/sway
printf 'xwayland disable\noutput HEADLESS-1 mode 1600x900 bg #4a4458 solid_color\n' >/tmp/sway.conf
/tmp/sway -c /tmp/sway.conf >/tmp/sway.log 2>&1 &
for _ in $(seq 50); do
    sock=$(ls "$XDG_RUNTIME_DIR" 2>/dev/null | grep -E '^wayland-[0-9]+$' | head -1)
    [ -n "$sock" ] && break
    sleep 0.1
done
export WAYLAND_DISPLAY="$sock"

if [ -n "${M3_FAKE_HYPR:-}" ]; then
    export HYPRLAND_INSTANCE_SIGNATURE=fake
    python3 tools/fake-hyprland.py &
    sleep 0.3
fi

if [ -n "${M3_GDB:-}" ]; then
    gdb -batch -ex run -ex "bt 30" ./build/material3ui >/tmp/m3.log 2>&1 &
else
    ./build/material3ui >/tmp/m3.log 2>&1 &
fi
sleep "${M3_WAIT:-2}"
./build/material3ui msg ping
[ -n "${M3_SCRIPT:-}" ] && eval "$M3_SCRIPT"
grim "/src/$out"
grep -v MESA /tmp/m3.log
