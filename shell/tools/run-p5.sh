M=./build/material3ui
$M msg lock; sleep 1.2; grim /src/p5-lock.png
wtype -k Shift_L "wrongpass" -s 200 -k Return; sleep 3.5; grim /src/p5-wrong.png
wtype -k Shift_L "test" -s 200 -k Return; sleep 3; $M msg ping; grim /src/p5-unlocked.png
grep -E "locked|unlocked|refused" /tmp/m3.log
