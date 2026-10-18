#!/usr/bin/env python3
"""A stand-in for greetd's IPC socket, for headless greeter tests.

The password is "secret". Requests and outcomes are printed.
"""
import json, os, socket, struct

path = os.environ["GREETD_SOCK"]
s = socket.socket(socket.AF_UNIX)
s.bind(path)
s.listen()
conn, _ = s.accept()


def reply(obj):
    data = json.dumps(obj).encode()
    conn.sendall(struct.pack("=I", len(data)) + data)


while True:
    head = conn.recv(4)
    if len(head) < 4:
        break
    (n,) = struct.unpack("=I", head)
    req = json.loads(conn.recv(n))
    print("greetd <-", json.dumps(req if req.get("type") != "post_auth_message_response" else {**req, "response": "***"}), flush=True)
    t = req["type"]
    if t == "create_session":
        reply({"type": "auth_message", "auth_message_type": "secret", "auth_message": "Password: "})
    elif t == "post_auth_message_response":
        if req.get("response") == "secret":
            reply({"type": "success"})
        else:
            reply({"type": "error", "error_type": "auth_error", "description": "pam: authentication failure"})
    elif t == "start_session":
        print("greetd: starting", req["cmd"], flush=True)
        reply({"type": "success"})
    elif t == "cancel_session":
        reply({"type": "success"})
