#!/bin/sh
# greetd entry point: the greeter in a minimal session of Hyprland, or of
# niri when Hyprland isn't installed.
DIR=/usr/share/material3ui-greeter
if command -v Hyprland >/dev/null 2>&1; then
    exec "$DIR/greet-hyprland.sh"
elif command -v niri >/dev/null 2>&1; then
    exec "$DIR/greet-niri.sh"
fi
echo "material3ui-greeter: neither Hyprland nor niri is installed" >&2
exit 1
