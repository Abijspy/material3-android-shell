#!/usr/bin/env bash
# Scrollbar, clipboard history and screenshots. Run with M3_SCALE=1.25 so the
# Settings window has to scroll:
#   M3_SERVICES=1 M3_FAKE_HYPR=1 M3_SCALE=1.25 M3_SCRIPT="bash tools/test-tools.sh" tools/dev.sh shot
M=./build/material3ui; P=./build/vpointer
echo "== settings scrollbar"
$M msg settings style; sleep 1.5; grim /src/tools-scroll.png
# Drag the thumb to the bottom (logical px; the pointer works in output px)
python3 tools/ui.py dump | grep -m1 ScrollView
$M msg settings; sleep 0.5
echo "== clipboard"
sleep 1
wl-copy "First copied line"; sleep 0.4
wl-copy "https://github.com/abijspy/material3-android-shell"; sleep 0.4
wl-copy < /usr/share/icons/hicolor/48x48/apps/$(ls /usr/share/icons/hicolor/48x48/apps | head -1) 2>/dev/null || true; sleep 0.4
wl-copy "The most recent thing"; sleep 0.6
cliphist list | head -5
$M msg clipboard; sleep 1.5; grim /src/tools-clip.png
wtype -k Shift_L "github"; sleep 0.8; grim /src/tools-clip-search.png
$M msg clipboard; sleep 0.5
echo "== screenshot tool"
$M msg screenshot; sleep 1.2; grim /src/tools-shot.png
read X Y < <(python3 tools/ui.py find Capture --exact --surface material3ui-popup); $P move $X $Y click left; sleep 2.5
ls ~/Pictures/Screenshots
$M msg screenshot screen; sleep 1.5
ls ~/Pictures/Screenshots | wc -l
$M msg screenshot; sleep 1.2; grim /src/tools-shot-recent.png
