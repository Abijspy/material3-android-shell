sleep 1; grim /src/p5-greeter.png
wtype -k Shift_L "nope" -s 200 -k Return; sleep 0.8
wtype -k Shift_L "secret" -s 200 -k Return; sleep 1
cat /tmp/greetd.log; pgrep -x material3ui >/dev/null && echo "greeter still running" || echo "greeter exited"
