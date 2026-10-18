#!/usr/bin/env bash
# v4.1: weather, bar widgets, lock screen widgets, greeter sync. Run via
#   M3_SERVICES=1 M3_FAKE_HYPR=1 M3_SCRIPT="bash tools/test-v41.sh" tools/dev.sh shot
M=./build/material3ui; P=./build/vpointer
cfg() { python3 - "$@" <<'PY'
import json, os, sys
p = os.path.expanduser("~/.config/material3ui/config.json")
c = json.load(open(p)) if os.path.exists(p) else {}
for kv in sys.argv[1:]:
    k, v = kv.split("=", 1)
    d = c
    *path, last = k.split(".")
    for x in path: d = d.setdefault(x, {})
    d[last] = json.loads(v)
os.makedirs(os.path.dirname(p), exist_ok=True)
json.dump(c, open(p + ".tmp", "w")); os.replace(p + ".tmp", p)
PY
}
echo "== weather (location from the IP address)"
for _ in $(seq 20); do $M msg status >/dev/null; [ -s ~/.local/state/material3ui/weather.json ] && break; sleep 1; done
grep -o '"place":"[^"]*"' ~/.local/state/material3ui/weather.json || echo "no weather yet"
$M msg settings weather; sleep 1.2
read X Y < <(python3 tools/ui.py at TextField --surface material3ui-popup); $P move $X $Y click left; sleep 0.3
wtype -k Shift_L "Chennai"; sleep 4; grim /src/v41-weather.png
read X Y < <(python3 tools/ui.py find Use --exact --surface material3ui-popup); $P move $X $Y click left; sleep 5
grep -o '"place":"[^"]*"' ~/.local/state/material3ui/weather.json
grim /src/v41-weather-chennai.png
$M msg settings; sleep 0.6
echo "== bar widgets"
cfg 'statusBar.layout={"left":["launcher","workspaces","weather","calendar"],"center":["island"],"right":["sysmon","media","notifications","quicksettings","battery"]}'
sleep 2; grim -g "0,0 1600,40" /src/v41-bar.png
echo "== dashboard"
$M msg popup dashboard; sleep 1.5; grim /src/v41-dashboard.png; $M msg popup dashboard; sleep 0.5
echo "== lock screen settings + lock"
$M msg settings lockscreen; sleep 1.2; grim /src/v41-lockpage.png; $M msg settings; sleep 0.5
cfg 'lock.widgets=["date","weather","forecast","battery","notifications"]' 'lock.clock.align="left"' 'lock.clock.style="inline"' 'lock.clock.size=128'
sleep 1; notify-send "Test" "a waiting notification"; sleep 0.5
$M msg lock; sleep 2.5; grim /src/v41-lock.png
pkill -x material3ui; sleep 0.5
echo "== greeter sync and preview"
mkdir -p /var/lib/material3ui-greeter/avatars
bash /repo/greeter/material3ui-greeter sync 2>&1 | tail -1
ls /var/lib/material3ui-greeter
PATH="$PWD/build:$PATH" sh /repo/greeter/greet-run.sh >/tmp/greeter.log 2>&1 &
sleep 3; grim /src/v41-greeter.png
