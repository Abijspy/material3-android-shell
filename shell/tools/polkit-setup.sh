# Sourced before the shell starts: a system bus, polkitd, the helper socket,
# and alice (in wheel, so polkit's default admin rule accepts her)
useradd -m -G wheel alice && echo alice:wonderland | chpasswd
mkdir -p /run/dbus
# A system-like bus that any user may connect to (no systemd in here)
cat > /tmp/system-bus.conf <<'XML'
<!DOCTYPE busconfig PUBLIC "-//freedesktop//DTD D-Bus Bus Configuration 1.0//EN"
 "http://www.freedesktop.org/standards/dbus/1.0/busconfig.dtd">
<busconfig>
  <type>system</type>
  <listen>unix:path=/run/dbus/system_bus_socket</listen>
  <auth>EXTERNAL</auth>
  <policy context="default">
    <allow user="*"/>
    <allow own="*"/>
    <allow send_destination="*"/>
    <allow receive_sender="*"/>
  </policy>
</busconfig>
XML
dbus-daemon --config-file=/tmp/system-bus.conf --fork
export DBUS_SYSTEM_BUS_ADDRESS=unix:path=/run/dbus/system_bus_socket
/usr/lib/polkit-1/polkitd --no-debug >/tmp/polkitd.log 2>&1 &
python3 tools/polkit-helper-socket.py >/tmp/helper.log 2>&1 &
sleep 0.8
