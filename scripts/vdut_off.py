#!/usr/bin/env python3
"""vdut_off.py — Send 'vdac off' to the TC to shut down VDUT1 (and VDUT2).

Safe to run any time — if VDUT is already off the TC simply confirms it.

Usage:
    python3 scripts/vdut_off.py
"""

import socket
import sys
import time

sys.path.insert(0, __import__("os").path.dirname(__import__("os").path.abspath(__file__)))
from tc_config import HOST, PORT


def tc_cmd(s: socket.socket, cmd: str, wait: float = 2.0) -> str:
    s.sendall((cmd + "\r\n").encode())
    out = b""
    deadline = time.time() + wait
    s.settimeout(0.5)
    while time.time() < deadline:
        try:
            chunk = s.recv(4096)
            if chunk:
                out += chunk
        except OSError:
            pass
    return out.decode("utf-8", errors="replace")


def main() -> None:
    try:
        s = socket.socket()
        s.settimeout(5)
        s.connect((HOST, PORT))
        time.sleep(0.2)
        # drain any banner/prompt
        s.settimeout(0.3)
        try:
            while s.recv(4096):
                pass
        except OSError:
            pass

        resp = tc_cmd(s, "vdac off")
        s.close()
    except OSError as e:
        print(f"FAIL: TC not reachable at {HOST}:{PORT} — {e}", file=sys.stderr)
        sys.exit(1)

    for line in resp.strip().splitlines():
        print(line)


if __name__ == "__main__":
    main()
