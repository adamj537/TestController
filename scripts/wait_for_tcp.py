#!/usr/bin/env python3
"""
wait_for_tcp.py — Poll TCP console until TCC comes online after flash/reboot.

Issue: S4/S5/S6/S7/S8 — used after every OTA or esptool flash to confirm
the device is back up and the TCP console is accepting connections.

Usage:
  python3 scripts/wait_for_tcp.py            # default: 10.0.0.244:4242
  python3 scripts/wait_for_tcp.py --timeout 60
"""
import socket
import sys
import time

TCC_IP      = '10.0.0.244'
TCC_PORT    = 4242
MAX_WAIT    = 45   # seconds
RETRY_DELAY = 1.5  # seconds between attempts


def main(timeout: float = MAX_WAIT) -> None:
    print(f'Waiting for TCP console at {TCC_IP}:{TCC_PORT} (max {timeout}s)...')
    deadline = time.time() + timeout
    attempt  = 0

    while time.time() < deadline:
        attempt += 1
        try:
            s = socket.socket()
            s.settimeout(2)
            s.connect((TCC_IP, TCC_PORT))
            s.settimeout(6)
            out = b''
            end = time.time() + 5
            while time.time() < end:
                try:
                    chunk = s.recv(512)
                    if chunk:
                        out += chunk
                except socket.timeout:
                    break
            s.close()
            elapsed = time.time() - (deadline - timeout)
            print(f'ONLINE after {elapsed:.1f}s (attempt {attempt})')
            banner = out.decode('utf-8', errors='replace').strip()
            if banner:
                print(banner[:400])
            return
        except Exception as e:
            print(f'  [{attempt}] {e}')
            time.sleep(RETRY_DELAY)

    print(f'ERROR: TCP console did not come online within {timeout}s')
    sys.exit(1)


if __name__ == '__main__':
    import argparse
    p = argparse.ArgumentParser(description='Wait for TCC TCP console')
    p.add_argument('--timeout', type=float, default=MAX_WAIT)
    args = p.parse_args()
    main(timeout=args.timeout)
