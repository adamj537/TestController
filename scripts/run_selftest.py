#!/usr/bin/env python3
"""Run a selftest command on a persistent TCP connection.

Usage:
    python3 scripts/run_selftest.py heartbeat [--host 192.168.50.30]
    python3 scripts/run_selftest.py dut_version
    python3 scripts/run_selftest.py mux
"""

import argparse
import socket
import sys
import time

HOST = "192.168.50.30"
PORT = 4242


def main() -> int:
    parser = argparse.ArgumentParser(description="Run selftest on persistent TCP")
    parser.add_argument("test", help="Selftest name (heartbeat, dut_version, mux, etc.)")
    parser.add_argument("--host", default=HOST)
    args = parser.parse_args()

    sock = socket.socket()
    sock.settimeout(10)
    try:
        sock.connect((args.host, PORT))
    except Exception as e:
        print(f"FAIL: {e}")
        return 1
    time.sleep(0.3)
    # Drain banner
    sock.settimeout(0.3)
    try:
        while sock.recv(4096):
            pass
    except socket.timeout:
        pass

    # Send selftest command
    cmd = f"selftest {args.test}"
    sock.sendall((cmd + "\n").encode())
    out: list[str] = []
    deadline = time.time() + 15
    sock.settimeout(1.0)
    while time.time() < deadline:
        try:
            chunk = sock.recv(8192).decode(errors="replace")
            if chunk:
                out.append(chunk)
                if "Results:" in chunk:
                    # Read trailing prompt
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

    print("".join(out).strip())
    sock.close()
    return 0


if __name__ == "__main__":
    sys.exit(main())
