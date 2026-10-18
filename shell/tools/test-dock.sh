#!/usr/bin/env bash
# Dock (autohide, then always) and the bar layout editor. Run via
#   M3_SERVICES=1 M3_FAKE_HYPR=1 M3_SCRIPT="bash tools/test-dock.sh" tools/dev.sh shot
M=./build/material3ui; P=./build/vpointer
echo "== autohide: hidden over windows, back at the edge"
grim /src/dock-hidden.png
# One pointer session: a device that exits sends a leave, which hides the dock
$P move 800 899 sleep 900 move 797 850 sleep 1500 move 800 400 sleep 1500 &
sleep 0.8; grim /src/dock-reveal.png
sleep 0.8; grim /src/dock-hover.png
sleep 1.6; grim /src/dock-rehidden.png; wait
echo "== always visible"
python3 - <<'PY'
import json, os
p = os.path.expanduser("~/.config/material3ui/config.json")
c = json.load(open(p)) if os.path.exists(p) else {}
c.setdefault("dock", {})["mode"] = "always"
json.dump(c, open(p + ".tmp", "w")); os.replace(p + ".tmp", p)
PY
sleep 1.5; grim /src/dock-always.png
$M msg debug | grep "material3ui-dock"
echo "== bar editor: add clipboard + screenshot on the right, move the clock"
$M msg settings bar; sleep 1.2
read X Y < <(python3 tools/ui.py find Add --exact --nth 0 --surface material3ui-popup); $P move $X $Y click left; sleep 0.8
read X Y < <(python3 tools/ui.py find Clipboard --exact --surface material3ui-popup); $P move $X $Y click left; sleep 0.8
read X Y < <(python3 tools/ui.py find Screenshot --exact --surface material3ui-popup); $P move $X $Y click left; sleep 0.8
read X Y < <(python3 tools/ui.py find "Network speed" --exact --surface material3ui-popup); $P move $X $Y click left; sleep 0.8
grim /src/dock-bareditor.png
grep -A12 '"layout"' ~/.config/material3ui/config.json | tr -d ' \n'; echo
$M msg settings; sleep 1; grim /src/dock-bar.png
