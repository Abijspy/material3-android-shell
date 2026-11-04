#!/usr/bin/env bash
# Every Settings page, Niri Settings page (on niri) and popup through
# `material3ui msg layout check`: anything spilling out of its row or card,
# overlapping a sibling, squeezed to nothing, a word cut short ("R…"), or
# off its surface. Exits 1 if
# anything is found.
#
# Run it against a shell already running (M3_INSTANCE picks a second one),
# best at its narrowest and with large text, where layouts break first:
# Settings → Accessibility → Text size, or /accessibility/textScale 1.5 in
# the config.json the instance under test reads.
#   M3_INSTANCE=test shell/tools/test-layout.sh [--wait SECONDS]
set -u
BIN="${M3_BIN:-material3ui}"
WAIT=3
[[ "${1:-}" == --wait ]] && WAIT="${2:-3}"
found=0

check() { # check LABEL: the open surfaces, the wallpaper left out
    local out
    out="$("$BIN" msg layout check 2>&1 | grep -v "^ok:" | grep -v "material3ui-wallpaper:" | grep -v " found (")"
    if [[ -n "$out" ]]; then
        printf '%s\n' "$out" | sed "s|^|$1 |"
        found=1
    fi
}

pages="network vpn bluetooth printers defaults notifications tools spotlight wellbeing calendar clock weather battery storage monitor
       sound display appearance style bar dock lockscreen accessibility lock firewall users system mouse keybinds hooks
       packages firmware about"
for page in $pages; do
    "$BIN" msg settings "$page" >/dev/null
    sleep "$WAIT"
    check "[settings $page]"
done

if [[ -n "${NIRI_SOCKET:-}" ]]; then
    for page in niri-layout niri-decoration niri-animations niri-behaviour niri-keys niri-rules niri-startup niri-environment; do
        "$BIN" msg nirisettings "$page" >/dev/null
        sleep "$WAIT"
        check "[niri $page]"
    done
fi

# The popups, each opened, checked and closed again
for popup in quicksettings notifications dashboard audio network bluetooth battery power clipboard clock timezone \
             phone updates weather wellbeing printers firewall vpn messages notes; do
    "$BIN" msg popup "$popup" >/dev/null
    sleep "$WAIT"
    check "[popup $popup]"
    "$BIN" msg popup "$popup" >/dev/null
    sleep 0.6
done
"$BIN" msg launcher >/dev/null
sleep "$WAIT"
check "[launcher]"
"$BIN" msg launcher >/dev/null

if [[ "$found" == 0 ]]; then echo "layout: everything inside its box"; fi
exit "$found"
