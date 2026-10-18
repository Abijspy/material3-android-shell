#!/usr/bin/env bash
# Runs inside the dev container: headless Sway, optionally a fake Hyprland,
# the shell, an optional command script, then a screenshot.
#   headless.sh <out.png>   env: M3_FAKE_HYPR=1, M3_SCRIPT="…", M3_WAIT=seconds
set -u
out="${1:-shot.png}"
[ -d build ] || meson setup build >/dev/null
build_log=$(meson compile -C build 2>&1) || { echo "$build_log" | grep -E "error|FAILED"; exit 1; }

export XDG_RUNTIME_DIR=/tmp/xdg
mkdir -p -m 700 "$XDG_RUNTIME_DIR"
export WLR_BACKENDS=headless WLR_RENDERER=pixman WLR_LIBINPUT_NO_DEVICES=1

# sway carries file capabilities, which a rootless container refuses to exec
cp /usr/bin/sway /tmp/sway
printf 'xwayland disable\noutput HEADLESS-1 mode 1600x900 scale %s bg #4a4458 solid_color\n' "${M3_SCALE:-1}" >/tmp/sway.conf
/tmp/sway -c /tmp/sway.conf >/tmp/sway.log 2>&1 &
for _ in $(seq 50); do
    sock=$(ls "$XDG_RUNTIME_DIR" 2>/dev/null | grep -E '^wayland-[0-9]+$' | head -1)
    [ -n "$sock" ] && break
    sleep 0.1
done
export WAYLAND_DISPLAY="$sock"

if [ -n "${M3_SERVICES:-}" ]; then
    # Real session bus, plus a private bus standing in for the system bus
    dbus-daemon --session --fork --address="unix:path=$XDG_RUNTIME_DIR/bus"
    export DBUS_SESSION_BUS_ADDRESS="unix:path=$XDG_RUNTIME_DIR/bus"
    dbus-daemon --config-file=/usr/share/dbus-1/session.conf --fork --address=unix:path=/tmp/sysbus
    export DBUS_SYSTEM_BUS_ADDRESS=unix:path=/tmp/sysbus
    python3 -u tools/mock-services.py system >/tmp/mock-system.log 2>&1 &
    # PipeWire with a stereo null sink as "Speakers"
    pipewire >/tmp/pw.log 2>&1 &
    sleep 0.3
    wireplumber >/tmp/wp.log 2>&1 &
    pipewire-pulse >/tmp/pwp.log 2>&1 &
    for _ in $(seq 30); do pactl info >/dev/null 2>&1 && break; sleep 0.2; done
    pactl load-module module-null-sink sink_name=speakers channel_map=front-left,front-right \
        sink_properties=device.description=Speakers >/dev/null
    pactl load-module module-null-sink sink_name=headphones channel_map=front-left,front-right \
        sink_properties=device.description=Headphones >/dev/null
    pactl set-default-sink speakers
    sleep 0.5
fi

if [ -n "${M3_FAKE_HYPR:-}" ]; then
    export HYPRLAND_INSTANCE_SIGNATURE=fake
    python3 tools/fake-hyprland.py &
    sleep 0.3
fi

[ -n "${M3_SETUP:-}" ] && eval "$M3_SETUP"
if [ -n "${M3_GDB:-}" ]; then
    gdb -batch -ex run -ex "bt 30" ./build/material3ui >/tmp/m3.log 2>&1 &
else
    ./build/material3ui ${M3_ARGS:-} >/tmp/m3.log 2>&1 &
fi
if [ -n "${M3_SERVICES:-}" ]; then
    python3 tools/mock-services.py session 1 >/tmp/mock-session.log 2>&1 &
fi
sleep "${M3_WAIT:-2}"
./build/material3ui msg ping
[ -n "${M3_SCRIPT:-}" ] && eval "$M3_SCRIPT"
grim "/src/$out"
grep -v MESA /tmp/m3.log
[ -n "${M3_SERVICES:-}" ] && cat /tmp/mock-session.log /tmp/mock-system.log
