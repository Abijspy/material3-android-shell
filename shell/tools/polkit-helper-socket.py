#!/usr/bin/env python3
"""Stands in for systemd's polkit-agent-helper.socket (Accept=yes) in the
test container: each connection runs polkit-agent-helper-1 as root with the
connection as stdin and stdout, as systemd would."""
import os, socket, subprocess

path = "/run/polkit/agent-helper.socket"
os.makedirs(os.path.dirname(path), exist_ok=True)
if os.path.exists(path):
    os.unlink(path)
s = socket.socket(socket.AF_UNIX)
s.bind(path)
os.chmod(path, 0o666)
s.listen()
while True:
    conn, _ = s.accept()
    subprocess.Popen(["/usr/lib/polkit-1/polkit-agent-helper-1", "--socket-activated"],
                     stdin=conn.fileno(), stdout=conn.fileno())
    conn.close()
