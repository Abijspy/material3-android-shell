#!/usr/bin/env python3
"""A stand-in for Hyprland's two IPC sockets, for headless shell tests.

Serves j/workspaces, j/monitors, j/activewindow and j/status on .socket.sock,
accepts `dispatch` (workspace switches) and streams events on .socket2.sock.
Run with HYPRLAND_INSTANCE_SIGNATURE and XDG_RUNTIME_DIR set.
"""
import json, os, re, socket, threading, time

sig = os.environ["HYPRLAND_INSTANCE_SIGNATURE"]
base = os.path.join(os.environ["XDG_RUNTIME_DIR"], "hypr", sig)
os.makedirs(base, exist_ok=True)

state = {
    "active": int(os.environ.get("FAKE_ACTIVE", "2")),
    "workspaces": {1: 2, 2: 1, 3: 0, 4: 3, 7: 1},  # id → windows
    "title": os.environ.get("FAKE_TITLE", "material3ui — Neovim"),
}
listeners = []
lock = threading.Lock()


def emit(line):
    with lock:
        for c in list(listeners):
            try:
                c.sendall((line + "\n").encode())
            except OSError:
                listeners.remove(c)


def reply(cmd):
    if cmd.startswith("j/"):
        what = cmd[2:]
        if what == "workspaces":
            return json.dumps([{"id": i, "name": str(i), "monitor": "HEADLESS-1", "windows": n}
                               for i, n in state["workspaces"].items()])
        if what == "monitors":
            return json.dumps([{"name": "HEADLESS-1", "focused": True,
                                "activeWorkspace": {"id": state["active"], "name": str(state["active"])}}])
        if what == "activewindow":
            return json.dumps({"title": state["title"], "class": "nvim"})
        if what == "status":
            return json.dumps({"configProvider": "lua"})
    m = re.search(r'workspace = "(\d+)"', cmd) or re.match(r"dispatch workspace (\d+)$", cmd)
    if cmd.startswith("dispatch") and m:
        state["active"] = int(m.group(1))
        state["workspaces"].setdefault(state["active"], 0)
        emit(f"workspace>>{state['active']}")
        return "ok"
    return "unknown request"


def serve_requests():
    path = os.path.join(base, ".socket.sock")
    s = socket.socket(socket.AF_UNIX)
    s.bind(path)
    s.listen()
    while True:
        c, _ = s.accept()
        data = c.recv(4096).decode()
        c.sendall(reply(data).encode())
        c.close()


def serve_events():
    path = os.path.join(base, ".socket2.sock")
    s = socket.socket(socket.AF_UNIX)
    s.bind(path)
    s.listen()
    while True:
        c, _ = s.accept()
        with lock:
            listeners.append(c)


threading.Thread(target=serve_events, daemon=True).start()
serve_requests()
