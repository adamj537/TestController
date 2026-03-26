#!/usr/bin/env python3
"""Power VDUT1 and hold PB-A asserted indefinitely.

Sends vdac_duty + mux select 0 0, then keeps a TCP connection open
so the TC stays aware we're here. Press Ctrl-C to release.
"""

import socket
import time

HOST = "10.0.0.244"
PORT = 4242
CMDS = [
    "vdac_duty 1 80",
    "mux select 0 0",
    "gpio get 38",   # read SWDIO to confirm DUT state
]

def send_cmd(s: socket.socket, cmd: str) -> str:
    s.sendall((cmd + "\n").encode())
    time.sleep(0.3)
    data = b""
    s.settimeout(2.0)
    try:
        while True:
            chunk = s.recv(4096)
            if not chunk:
                break
            data += chunk
    except socket.timeout:
        pass
    return data.decode(errors="replace")

def main() -> None:
    print(f"Connecting to TC at {HOST}:{PORT}...")
    with socket.socket(socket.AF_INET, socket.SOCK_STREAM) as s:
        s.connect((HOST, PORT))
        s.settimeout(3.0)
        # drain banner
        try:
            s.recv(4096)
        except socket.timeout:
            pass

        for cmd in CMDS:
            resp = send_cmd(s, cmd)
            print(f">>> {cmd}")
            for line in resp.splitlines():
                if line.strip() and "g3-tc|" not in line:
                    print(f"    {line}")

        print("\nDUT powered, PB-A asserted. Press Ctrl-C to release.")
        try:
            while True:
                time.sleep(5)
                # keepalive: send empty newline so TC doesn't time out
                try:
                    s.sendall(b"\n")
                    s.recv(256)
                except Exception:
                    pass
        except KeyboardInterrupt:
            print("\nReleasing — sending mux select 1 0")
            send_cmd(s, "mux select 1 0")

if __name__ == "__main__":
    main()
