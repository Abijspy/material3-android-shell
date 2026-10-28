#!/bin/sh
# A minimal niri session that hosts the Material3UI greeter.
export XDG_SESSION_TYPE=wayland
export XDG_CURRENT_DESKTOP=niri
exec niri -c /usr/share/material3ui-greeter/niri.kdl
