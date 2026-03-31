#!/usr/bin/env python3
"""recipe_run_capture.py — Run a recipe and capture all console output.

Usage:
    python3 scripts/recipe_run_capture.py [--recipe g3-mb-v2]
"""
import argparse
import socket
import sys
import os
import time

sys.path.insert(0, os.path.dirname(__file__))
from tc_config import HOST, PORT


def drain(sock: socket.socket) -> str:
    buf = b""
    sock.settimeout(0.3)
    try:
        while True:
            chunk = sock.recv(4096)
            if not chunk:
                break
            buf += chunk
    except socket.timeout:
        pass
    return buf.decode("utf-8", errors="replace")


def cmd(sock: socket.socket, command: str, wait: float = 1.0) -> str:
    sock.sendall((command + "\r\n").encode())
    time.sleep(wait)
    return drain(sock)


def main() -> None:
    p = argparse.ArgumentParser(description="Run recipe and capture output")
    p.add_argument("--recipe", default="g3-mb-v2")
    p.add_argument("--timeout", type=int, default=120, help="Max seconds to wait")
    args = p.parse_args()

    print(f"Connecting to {HOST}:{PORT} ...", file=sys.stderr)
    s = socket.socket()
    s.settimeout(5)
    s.connect((HOST, PORT))
    drain(s)

    # Select recipe
    resp = cmd(s, f"recipe select {args.recipe}", wait=2.0)
    print(f"Recipe selected: {args.recipe}", file=sys.stderr)

    # Start
    s.sendall(b"sm start\r\n")

    buf = b""
    start = time.time()
    s.settimeout(2)
    terminal_seen = False
    while time.time() - start < args.timeout:
        try:
            chunk = s.recv(4096)
            if not chunk:
                break
            buf += chunk
            text = chunk.decode("utf-8", errors="replace")
            sys.stdout.write(text)
            sys.stdout.flush()
            # Check for terminal states
            if b"recipe_eng" in buf[-500:] and b"complete:" in buf[-500:]:
                terminal_seen = True
            if terminal_seen or b"Fail\r\n" in buf[-200:] or b"Pass\r\n" in buf[-200:]:
                # Give a moment for final output
                time.sleep(2)
                rest = drain(s)
                sys.stdout.write(rest)
                sys.stdout.flush()
                break
        except socket.timeout:
            continue

    s.close()
    elapsed = time.time() - start
    print(f"\n--- Capture complete ({elapsed:.1f}s) ---", file=sys.stderr)


if __name__ == "__main__":
    main()
