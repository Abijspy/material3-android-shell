#!/bin/sh
# Starts the native greeter with the appearance synced by `material3ui-greeter
# sync`: the desktop user's palette and wallpaper.
DATA=/var/lib/material3ui-greeter
[ -f "$DATA/colors.json" ] && export M3_COLORS_FILE="$DATA/colors.json"
for w in "$DATA"/wallpaper.*; do
    [ -f "$w" ] && export M3_WALLPAPER="$w" && break
done
exec material3ui greeter
