#!/usr/bin/env bash
# v4.1.1: on-bar edit mode and the lock screen grid editor. Run via
#   M3_SERVICES=1 M3_FAKE_HYPR=1 M3_SCRIPT="bash tools/test-v411.sh" tools/dev.sh shot
M=./build/material3ui; P=./build/vpointer
ui() { python3 tools/ui.py "$@"; }
echo "== bar edit mode"
$M msg bar edit; sleep 1; grim -g "0,0 1600x48" /src/v411-baredit.png
read X Y < <(ui find add --exact --nth 0 --surface material3ui-bar); $P move $X $Y click left; sleep 1.2; grim /src/v411-picker.png
read X Y < <(ui find Weather --exact --surface material3ui-popup); $P move $X $Y click left; sleep 1.5
read X Y < <(ui find close --exact --nth 1 --surface material3ui-bar); $P move $X $Y click left; sleep 1.5
python3 -c "import json,os;print(json.load(open(os.path.expanduser('~/.config/material3ui/config.json')))['statusBar']['layout'])"
grim -g "0,0 1600x48" /src/v411-baredit2.png
read X Y < <(ui find Done --exact --surface material3ui-bar); $P move $X $Y click left; sleep 1.2
grim -g "0,0 1600x48" /src/v411-bardone.png
echo "== lock grid editor"
$M msg settings lockscreen; sleep 1.3
# Scroll the page to the board
read X Y < <(ui find Layout --exact --surface material3ui-popup); $P move $X $Y scroll 6 sleep 300; sleep 0.8
grim /src/v411-grid.png
ui dump | grep -c "'Button'" >/dev/null
# Select the clock and move it right; put Weather in the top-left cell
read X Y < <(ui find Clock --exact --nth 1 --surface material3ui-popup); echo "clock at $X $Y"; $P move $X $Y click left; sleep 0.8
read X Y < <(ui find arrow_forward --exact --surface material3ui-popup); $P move $X $Y click left; sleep 1.2
read X Y < <(ui find add --exact --nth 0 --surface material3ui-popup); $P move $X $Y click left; sleep 0.8
grim /src/v411-grid-add.png
read X Y < <(ui find "Temperature and sky now" --exact --surface material3ui-popup); $P move $X $Y click left; sleep 1.2
python3 -c "import json,os;print(json.load(open(os.path.expanduser('~/.config/material3ui/config.json')))['lock']['grid'])"
grim /src/v411-grid2.png
$M msg settings; sleep 0.6
$M msg lock; sleep 2.5; grim /src/v411-lock.png
