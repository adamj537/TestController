#!/usr/bin/env python3
"""ota_flash.py — Pause DUT detect, OTA flash new firmware, wait for reboot.

Usage:
    python3 scripts/ota_flash.py [--host 10.0.0.244] [--url http://10.0.0.100:8080/firmware.bin]
"""

import argparse
import socket
import subprocess
import sys
import time

import sys, os
sys.path.insert(0, os.path.dirname(__file__))
from tc_config import HOST, PORT

def _wsl_ip():
    try:
        out = subprocess.check_output(["hostname", "-I"], text=True).split()
        return out[0] if out else "127.0.0.1"
    except Exception:
        return "127.0.0.1"

DEFAULT_URL = f"http://{_wsl_ip()}:8080/firmware.bin"


def cmd(sock, command, wait=3.0):
    sock.sendall((command + "\n").encode())
    out = []
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


def main():
    parser = argparse.ArgumentParser(description="OTA flash TC firmware")
    parser.add_argument("--host", default=HOST)
    parser.add_argument("--url", default=DEFAULT_URL)
    args = parser.parse_args()

    print(f"[1] Connecting to {args.host}:{PORT}")
    sock = socket.socket()
    sock.settimeout(10)
    try:
        sock.connect((args.host, PORT))
    except Exception as e:
        print(f"  FAIL: {e}")
        sys.exit(1)

    time.sleep(0.5)
    # drain banner
    sock.settimeout(0.5)
    try:
        while sock.recv(4096):
            pass
    except socket.timeout:
        pass
    sock.settimeout(10)
    print("  Connected")

    print("\n[2] Pausing DUT auto-start")
    resp = cmd(sock, "dut pause", wait=2)
    print(f"  {resp.strip()[:120]}")

    print(f"\n[3] Starting OTA: {args.url}")
    sock.sendall(f"ota update {args.url}\n".encode())

    # Wait for OTA completion — watch for success/fail/reboot
    out = []
    deadline = time.time() + 120
    sock.settimeout(2)
    while time.time() < deadline:
        try:
            chunk = sock.recv(4096).decode(errors="replace")
            if chunk:
                print(chunk, end="", flush=True)
                out.append(chunk)
                text = "".join(out)
                if "rebooting" in text.lower():
                    print("\n  OTA complete — TC is rebooting")
                    break
                if "OTA failed" in text or "ota update: error" in text.lower():
                    print("\n  OTA FAILED")
                    sock.close()
                    sys.exit(1)
        except socket.timeout:
            continue
        except (ConnectionResetError, BrokenPipeError):
            print("\n  Connection lost (TC rebooting)")
            break

    sock.close()

    # Wait for TC to come back up
    print("\n[4] Waiting for TC to reboot...")
    time.sleep(5)
    for attempt in range(30):
        try:
            s = socket.socket()
            s.settimeout(3)
            s.connect((args.host, PORT))
            time.sleep(0.5)
            s.settimeout(2)
            try:
                banner = s.recv(4096).decode(errors="replace")
                print(f"  TC is back: {banner.strip()[:120]}")
            except socket.timeout:
                print("  TC is back (no banner)")
            s.close()
            print("\n[5] OTA flash complete")
            sys.exit(0)
        except (socket.timeout, ConnectionRefusedError, OSError):
            time.sleep(2)

    print("  TIMEOUT waiting for TC to come back")
    sys.exit(1)


if __name__ == "__main__":
    main()
