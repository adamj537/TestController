#!/usr/bin/env python3
"""sm_start_debug.py — Trigger sm start and capture detailed pre-gate log.

Selects the active recipe, starts the state machine, and captures all
console output for ~15 seconds to diagnose pre-gate failures.

Usage:
    python3 scripts/sm_start_debug.py [--recipe g3-mb-v2]
"""
import argparse
import socket
import sys
import os
import time

sys.path.insert(0, os.path.dirname(__file__))
from tc_config import HOST, PORT


def cmd(sock: socket.socket, command: str, wait: float = 1.0) -> str:
    sock.sendall((command + "\r\n").encode())
    buf = b""
    deadline = time.time() + wait
    while time.time() < deadline:
        try:
            chunk = sock.recv(4096)
            if chunk:
                buf += chunk
        except socket.timeout:
            continue
    return buf.decode("utf-8", errors="replace")


def main() -> None:
    p = argparse.ArgumentParser(description="Debug sm start / pre-gate")
    p.add_argument("--recipe", default="g3-mb-v2")
    args = p.parse_args()

    print(f"Connecting to {HOST}:{PORT} ...")
    s = socket.socket()
    s.settimeout(15)
    s.connect((HOST, PORT))
    time.sleep(0.3)
    s.settimeout(0.5)
    try:
        while s.recv(4096):
            pass
    except socket.timeout:
        pass

    # Select recipe
    print(f"Selecting recipe: {args.recipe}")
    s.settimeout(2)
    resp = cmd(s, f"recipe select {args.recipe}", wait=2.0)
    print(resp.strip())

    # Start state machine
    print("\nStarting state machine ...")
    s.sendall(b"sm start\r\n")

    buf = b""
    start = time.time()
    s.settimeout(2)
    while time.time() - start < 15:
        try:
            chunk = s.recv(4096)
            if not chunk:
                break
            buf += chunk
            text = chunk.decode("utf-8", errors="replace")
            for line in text.splitlines():
                if line.strip():
                    print(line)
            # Stop if we see a terminal state
            if b"Fail" in buf or b"Pass" in buf or b"Idle" in buf[100:]:
                time.sleep(1)
                try:
                    chunk = s.recv(4096)
                    if chunk:
                        for line in chunk.decode("utf-8", errors="replace").splitlines():
                            if line.strip():
                                print(line)
                except socket.timeout:
                    pass
                break
        except socket.timeout:
            continue

    s.close()
    elapsed = time.time() - start
    print(f"\nCapture complete ({elapsed:.1f}s)")


if __name__ == "__main__":
    main()
