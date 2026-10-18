#!/bin/sh
# greetd entry point: a minimal Hyprland session that hosts the Material3UI greeter.
export XDG_SESSION_TYPE=wayland
export XDG_CURRENT_DESKTOP=Hyprland
export QT_QPA_PLATFORM=wayland
export QT_WAYLAND_DISABLE_WINDOWDECORATION=1

CONFIG=/usr/share/material3ui-greeter/hyprland.lua
if command -v start-hyprland >/dev/null 2>&1; then
    exec start-hyprland -- -c "$CONFIG"
else
    exec Hyprland -c "$CONFIG"
fi
