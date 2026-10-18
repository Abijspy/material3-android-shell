#!/usr/bin/env bash
# Look & feel, status bar and network details. Run via
#   M3_SERVICES=1 M3_FAKE_HYPR=1 M3_WAIT=4 M3_SCRIPT="bash tools/test-look.sh" tools/dev.sh shot
M=./build/material3ui; P=./build/vpointer
# Traffic on the loopback, which the NM mock reports as the Wi-Fi interface
python3 -c "
import socket,time
s=socket.socket(socket.AF_INET,socket.SOCK_DGRAM)
while True:
    for _ in range(40): s.sendto(b'x'*8000,('127.0.0.1',9))
    time.sleep(0.02)" &
echo "== network page"
$M msg settings network; sleep 3; grim /src/look-network.png
echo "== look & feel"
$M msg settings; sleep 0.6
$M msg settings style; sleep 1.2; grim /src/look-style.png
read X Y < <(python3 tools/ui.py find Sharp --exact --surface material3ui-popup); $P move $X $Y click left; sleep 0.8
grep -o '"roundness": *[0-9.]*' ~/.config/material3ui/config.json
grim /src/look-sharp.png
read X Y < <(python3 tools/ui.py find Round --exact --surface material3ui-popup); $P move $X $Y click left; sleep 0.6
echo "== status bar page: move the bar to the bottom"
$M msg settings; sleep 0.6
$M msg settings bar; sleep 1.2
read X Y < <(python3 tools/ui.py find Bottom --exact --surface material3ui-popup); $P move $X $Y click left; sleep 1.2
grim /src/look-bar.png
$M msg debug | grep "material3ui-bar"
$M msg settings; sleep 0.6
echo "== quick settings"
$M msg popup quicksettings; sleep 2.5; grim /src/look-qs.png
read X Y < <(python3 tools/ui.py find "866 Mb/s" --surface material3ui-popup); $P move $X $Y click left; sleep 2.5
grim /src/look-qs-wifi.png
