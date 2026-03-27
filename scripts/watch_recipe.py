#!/usr/bin/env python3
"""watch_recipe.py — Trigger sm start and stream full TC console output.

Usage:
    python3 scripts/watch_recipe.py [timeout_seconds]

Connects to the TC TCP console, sends 'sm start', and streams all output
until the SM returns to Idle or timeout expires.  Useful for seeing the
detailed per-step console log (pre-gate current, dut_program output, etc.).
"""
import socket
import sys
import time
import os

sys.path.insert(0, os.path.dirname(__file__))
from tc_config import HOST, PORT

TIMEOUT = int(sys.argv[1]) if len(sys.argv) > 1 else 90


def main() -> int:
    sock = socket.socket()
    sock.settimeout(10)
    try:
        sock.connect((HOST, PORT))
    except Exception as e:
        print(f"Connect failed: {e}", file=sys.stderr)
        return 1

    time.sleep(0.3)
    sock.settimeout(0.5)
    try:
        while sock.recv(4096):
            pass
    except socket.timeout:
        pass

    print(f">>> sm start  (streaming for up to {TIMEOUT}s)")
    sock.sendall(b"sm start\n")

    deadline = time.time() + TIMEOUT
    buf = ""
    while time.time() < deadline:
        try:
            chunk = sock.recv(4096).decode("utf-8", errors="replace")
            if chunk:
                print(chunk, end="", flush=True)
                buf += chunk
                if "Testing \u2192 Idle" in buf or "Testing -> Idle" in buf:
                    time.sleep(1.0)
                    try:
                        tail = sock.recv(4096).decode("utf-8", errors="replace")
                        if tail:
                            print(tail, end="", flush=True)
                    except socket.timeout:
                        pass
                    break
        except socket.timeout:
            continue

    sock.close()
    return 0


if __name__ == "__main__":
    sys.exit(main())
