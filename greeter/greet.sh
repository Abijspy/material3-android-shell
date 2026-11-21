#!/bin/sh
# greetd entry point: the greeter in a minimal niri session.
DIR=/usr/share/material3ui-greeter
if command -v niri >/dev/null 2>&1; then
    exec "$DIR/greet-niri.sh"
fi
echo "material3ui-greeter: niri isn't installed" >&2
exit 1
