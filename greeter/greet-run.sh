#!/bin/sh
# Starts the native greeter with the appearance synced by `material3ui-greeter
# sync`: the desktop user's palette, wallpaper, lock screen and weather.
DATA=/var/lib/material3ui-greeter
[ -f "$DATA/colors.json" ] && export M3_COLORS_FILE="$DATA/colors.json"
# The lock screen's clock and widgets, and the last weather (the greeter
# refreshes it when the network is up)
[ -f "$DATA/config.json" ] && export M3_CONFIG_FILE="$DATA/config.json"
export M3_WEATHER_FILE="$DATA/weather.json"
for w in "$DATA"/wallpaper.*; do
    [ -f "$w" ] && export M3_WALLPAPER="$w" && break
done
exec material3ui greeter
