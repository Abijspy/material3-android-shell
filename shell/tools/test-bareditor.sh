#!/usr/bin/env bash
# Settings → Status bar: three columns, dropdown add, select/group/delete. Run via
#   M3_SERVICES=1 M3_FAKE_HYPR=1 M3_SCRIPT="bash tools/test-bareditor.sh" tools/dev.sh shot
M=./build/material3ui; P=./build/vpointer
ui() { python3 tools/ui.py "$@"; }
lay() { python3 -c "import json,os;print(json.load(open(os.path.expanduser('~/.config/material3ui/config.json')))['statusBar']['layout'])"; }
click() { read X Y < <(ui find "$@" --surface material3ui-popup); [ -n "$X" ] && $P move $X $Y click left; sleep 1.1; }
$M msg settings bar; sleep 1.3
read X Y < <(ui find Widgets --exact --surface material3ui-popup); $P move $X $Y scroll 5 sleep 300; sleep 0.8
grim /src/be-columns.png
echo "== add Weather from the right column's dropdown"
click "Add widget" --exact --nth 2
grim /src/be-dropdown.png
click Weather --exact --nth 1
lay
echo "== select Battery and Weather (right column), group them"
# checkboxes in tree order: left 2, middle 1, right: tray, status, battery, weather
click check_box_outline_blank --exact --nth 5
click check_box_outline_blank --exact --nth 5
grim /src/be-selected.png
click "Group 2" --exact
lay
grim /src/be-grouped.png
echo "== delete the tray with its row's delete button"
click delete --exact --nth 3
lay
$M msg settings; sleep 1; grim -g "0,0 1600x48" /src/be-bar.png
