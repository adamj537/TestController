#!/usr/bin/env python3
"""Diagnostic: power DUT, enter test mode, then sample heartbeat.

Uses a single persistent TCP connection to avoid banner/prompt confusion.

Usage:
    python3 scripts/diag_heartbeat.py --host 192.168.50.30
"""

import argparse
import socket
import sys
import time

HOST = "192.168.50.30"
PORT = 4242


def cmd(sock: socket.socket, command: str, wait: float = 3.0) -> str:
    """Send command and collect response until prompt or timeout."""
    sock.sendall((command + "\n").encode())
    out: list[str] = []
    deadline = time.time() + wait
    sock.settimeout(1.0)
    while time.time() < deadline:
        try:
            chunk = sock.recv(8192).decode(errors="replace")
            if chunk:
                out.append(chunk)
                if "g3-tc|" in chunk and ">" in chunk:
                    break
        except socket.timeout:
            continue
    return "".join(out)


def cmd_long(sock: socket.socket, command: str, stop_marker: str = "Results:",
             timeout: float = 10.0) -> str:
    """Send command and collect until stop_marker or timeout."""
    sock.sendall((command + "\n").encode())
    out: list[str] = []
    deadline = time.time() + timeout
    sock.settimeout(1.0)
    while time.time() < deadline:
        try:
            chunk = sock.recv(8192).decode(errors="replace")
            if chunk:
                out.append(chunk)
                if stop_marker in chunk:
                    # Read a bit more for trailing prompt
                    time.sleep(0.3)
                    try:
                        chunk = sock.recv(8192).decode(errors="replace")
                        if chunk:
                            out.append(chunk)
                    except socket.timeout:
                        pass
                    break
        except socket.timeout:
            continue
    return "".join(out)


def main() -> int:
    parser = argparse.ArgumentParser(description="Heartbeat diagnostic")
    parser.add_argument("--host", default=HOST)
    parser.add_argument("--port", type=int, default=PORT)
    args = parser.parse_args()

    print(f"[1] Connect to {args.host}:{args.port}")
    sock = socket.socket()
    sock.settimeout(10)
    try:
        sock.connect((args.host, args.port))
    except Exception as e:
        print(f"  FAIL: {e}")
        return 1
    time.sleep(0.3)
    # Drain banner
    sock.settimeout(0.3)
    try:
        while sock.recv(4096):
            pass
    except socket.timeout:
        pass

    print("[2] vdac off (clean slate)")
    print(cmd(sock, "vdac off", wait=3).strip())

    print("\n[3] dut pause")
    print(cmd(sock, "dut pause", wait=2).strip())

    print("\n[4] vdac_voltage 1 3300 (VDUT1 only — VDUT2 does not power DUT)")
    print(cmd(sock, "vdac_voltage 1 3300", wait=5).strip())

    print("\n[5] mux select 0 0 (PB-A)")
    print(cmd(sock, "mux select 0 0", wait=2).strip())

    print("\n[6] Wait 3s for DUT boot...")
    time.sleep(3)

    print("\n[7] selftest dut_version (ENTER_TEST → VERSION → EXIT_TEST)")
    resp = cmd_long(sock, "selftest dut_version", stop_marker="Results:", timeout=12)
    print(resp.strip())

    print("\n[8] Wait 2s after test mode cycle...")
    time.sleep(2)

    print("\n[9] selftest heartbeat (2.5s sample)")
    resp = cmd_long(sock, "selftest heartbeat", stop_marker="Results:", timeout=10)
    print(resp.strip())

    print("\n[10] Teardown")
    cmd(sock, "mux release", wait=2)
    cmd(sock, "vdac off", wait=3)
    cmd(sock, "dut resume", wait=2)
    print("  PB-A released, VDUT off, auto-start resumed")

    sock.close()
    return 0


if __name__ == "__main__":
    sys.exit(main())
