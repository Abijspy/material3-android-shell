#!/usr/bin/env bash
# Settings interactions: config chip, AUR search. Run via
#   M3_SERVICES=1 M3_WAIT=4 M3_SCRIPT="bash tools/test-settings.sh" tools/dev.sh shot
M=./build/material3ui; P=./build/vpointer
echo "== appearance chip writes config"
$M msg settings appearance; sleep 1.2
read X Y < <(python3 tools/ui.py find Wipe --exact --surface material3ui-popup); $P move $X $Y click left; sleep 0.6
grep -o '"wallpaperTransition": *"[a-z]*"' ~/.config/material3ui/config.json
$M msg settings; sleep 0.5
echo "== AUR search"
$M msg settings packages; sleep 1.2
read X Y < <(python3 tools/ui.py find AUR --exact --surface material3ui-popup); $P move $X $Y click left; sleep 0.8
wtype -k Shift_L "niri-git" -s 200; sleep 4; grim /src/set-aur.png
python3 tools/ui.py dump | grep -E "niri" | head -3
