#!/usr/bin/env bash
# Wi-Fi details inside the connected entry: Settings and Quick Settings
M=./build/material3ui; P=./build/vpointer
python3 -c "
import socket,time
s=socket.socket(socket.AF_INET,socket.SOCK_DGRAM)
while True:
    for _ in range(20): s.sendto(b'x'*6000,('127.0.0.1',9))
    time.sleep(0.02)" &
$M msg settings network; sleep 3; grim /src/net-settings.png
$M msg settings; sleep 0.6
$M msg popup quicksettings; sleep 1.5
read X Y < <(python3 tools/ui.py find "866 Mb/s" --surface material3ui-popup); $P move $X $Y click left; sleep 2.5
grim /src/net-qs.png
