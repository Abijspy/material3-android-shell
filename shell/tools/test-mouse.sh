#!/usr/bin/env bash
# Pointer-driven UI test: run via
#   M3_SERVICES=1 M3_FAKE_HYPR=1 M3_WAIT=4 M3_SCRIPT="bash tools/test-mouse.sh" tools/dev.sh shot
M=./build/material3ui; P=./build/vpointer
at() { python3 tools/ui.py "$@"; }
echo "== island hover, then skip (one device: a new one would send a leave)"
read X Y < <(at at Island)
$P move $X $Y sleep 900 move 937 20 sleep 300 click left sleep 600 &
sleep 1.0; grim /src/m-island.png; wait
$M msg media
$P move 800 600; sleep 0.8
echo "== tray"
read X Y < <(at at TrayIcon); $P move $X $Y click left; sleep 0.4
read X Y < <(at at TrayIcon --nth 1); $P move $X $Y click right; sleep 1; grim /src/m-traymenu.png
read X Y < <(at find "Show in tray" --surface material3ui-popup); $P move $X $Y click left; sleep 0.6
echo "== workspace dot"
read X Y < <(at at Dot --nth 3); $P move $X $Y click left; sleep 0.6
$M msg workspaces | grep active
echo "== volume slider drag"
$M msg volume set 100; sleep 0.3
read X Y < <(at find volume_up --surface material3ui-bar); $P move $X $Y click left; sleep 0.9
read X Y < <(at at Slider --surface material3ui-popup); W=$(at dump | awk '/material3ui-popup/{p=1} p && /Slider/{print; exit}' | grep -o "'w': [0-9.]*" | cut -d' ' -f2)
L=$(python3 -c "print(int($X - ${W:-300}/2))"); T=$(python3 -c "print(int($X - ${W:-300}/2 + ${W:-300}*0.3))")
$P move $X $Y down left sleep 100 move $T $Y sleep 100 up left; sleep 0.5; grim /src/m-slider.png
$M msg volume
echo "== click outside closes"
$P move 200 700 click left; sleep 0.6
$M msg debug | grep -c "material3ui-popup" || true
echo "== wifi tap and password"
read X Y < <(at find network_wifi --surface material3ui-bar); $P move $X $Y click left; sleep 0.9
read X Y < <(at find chevron_right --surface material3ui-popup); $P move $X $Y click left; sleep 1.2
read X Y < <(at find Office --surface material3ui-popup); $P move $X $Y click left; sleep 0.8
wtype -k Shift_L "hunter22" -s 300; sleep 0.3; grim /src/m-wifi.png
read X Y < <(at find Connect --exact --surface material3ui-popup); $P move $X $Y click left; sleep 1
grep -E "add-and-activate|activate" /tmp/mock-system.log
grep -E "tray activate|menu event" /tmp/mock-session.log
