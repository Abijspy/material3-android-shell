#!/usr/bin/env bash
# Settings interactions: keybind remap, config chip, AUR search. Run via
#   M3_SERVICES=1 M3_FAKE_HYPR=1 M3_WAIT=4 M3_SCRIPT="bash tools/test-settings.sh" tools/dev.sh shot
M=./build/material3ui; P=./build/vpointer
echo "== keybind remap: File manager → Super+X"
$M msg settings keybinds; sleep 1.2
read X Y < <(python3 tools/ui.py find "File manager" --exact --surface material3ui-popup); $P move $X $Y click left; sleep 0.8
read X Y < <(python3 tools/ui.py at KeyField --surface material3ui-popup); $P move $X $Y click left; sleep 0.4
wtype -k Shift_L -k x; sleep 0.6; grim /src/set-keyedit.png
read X Y < <(python3 tools/ui.py find Save --exact --surface material3ui-popup); $P move $X $Y click left; sleep 1.2
cat ~/.config/hypr/material3ui/keybinds_custom.lua; cat ~/.config/material3ui/keybinds.json | head -8
$M msg settings; sleep 0.5
echo "== appearance chip writes config"
$M msg settings appearance; sleep 1.2
read X Y < <(python3 tools/ui.py find Wipe --exact --surface material3ui-popup); $P move $X $Y click left; sleep 0.6
grep -o '"wallpaperTransition": *"[a-z]*"' ~/.config/material3ui/config.json
$M msg settings; sleep 0.5
echo "== AUR search"
$M msg settings packages; sleep 1.2
read X Y < <(python3 tools/ui.py find AUR --exact --surface material3ui-popup); $P move $X $Y click left; sleep 0.8
wtype -k Shift_L "hyprland-git" -s 200; sleep 4; grim /src/set-aur.png
python3 tools/ui.py dump | grep -E "hyprland" | head -3
