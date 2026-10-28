#!/bin/sh
# A minimal Hyprland session that hosts the Material3UI greeter.
# greetd configs from before 6.0 start here: niri, when Hyprland is gone
command -v Hyprland >/dev/null 2>&1 || exec /usr/share/material3ui-greeter/greet-niri.sh
export XDG_SESSION_TYPE=wayland
export XDG_CURRENT_DESKTOP=Hyprland

CONFIG=/usr/share/material3ui-greeter/hyprland.lua
if command -v start-hyprland >/dev/null 2>&1; then
    exec start-hyprland -- -c "$CONFIG"
else
    exec Hyprland -c "$CONFIG"
fi
