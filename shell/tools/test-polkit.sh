#!/usr/bin/env bash
# End-to-end polkit test: real polkitd asks the shell's agent for alice's
# password. Run via (see the README):
#   M3_USER=alice M3_SETUP='source tools/polkit-setup.sh' M3_SCRIPT='bash tools/test-polkit.sh' tools/dev.sh shot
pid=$(pgrep -u alice -x material3ui)
echo "agent: $(grep -o 'polkit.*' /tmp/m3.log | head -1)"
su alice -s /bin/bash -c "DBUS_SYSTEM_BUS_ADDRESS=$DBUS_SYSTEM_BUS_ADDRESS pkcheck --action-id org.freedesktop.policykit.exec \
    --process $pid --allow-user-interaction; echo pkcheck exit \$?" >/tmp/pkcheck.log 2>&1 &
sleep 2; grim /src/pk-dialog.png
wtype -k Shift_L "letmein" -s 200 -k Return; sleep 3; grim /src/pk-wrong.png
wtype -k Shift_L "wonderland" -s 200 -k Return; sleep 3
cat /tmp/pkcheck.log
