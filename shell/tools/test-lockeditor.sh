#!/usr/bin/env bash
# The full-screen lock screen editor. Run via
#   M3_SERVICES=1 M3_FAKE_HYPR=1 M3_SCRIPT="bash tools/test-lockeditor.sh" tools/dev.sh shot
M=./build/material3ui; P=./build/vpointer
ui() { python3 tools/ui.py "$@"; }
for _ in $(seq 20); do [ -s ~/.local/state/material3ui/weather.json ] && break; sleep 1; done
$M msg popup lockeditor; sleep 2; grim /src/le-open.png
read X Y < <(ui find Add --exact --nth 0 --surface material3ui-popup); echo "add at $X $Y"; $P move $X $Y click left; sleep 1
grim /src/le-select.png
read X Y < <(ui find Battery --exact --surface material3ui-popup); $P move $X $Y click left; sleep 1.5
python3 -c "import json,os;print(json.load(open(os.path.expanduser('~/.config/material3ui/config.json')))['lock']['grid'])"
read X Y < <(ui find arrow_downward --exact --surface material3ui-popup); $P move $X $Y click left; sleep 1.5
python3 -c "import json,os;print([i for i in json.load(open(os.path.expanduser('~/.config/material3ui/config.json')))['lock']['grid'] if i['id']=='battery'])"
grim /src/le-moved.png
read X Y < <(ui find Done --exact --surface material3ui-popup); $P move $X $Y click left; sleep 0.8
$M msg lock; sleep 2.5; grim /src/le-lock.png
