#!/usr/bin/env python3
"""
health_check.py — Post-flash health check: OTA partition status + full selftest.

Issue: S6/S7 — Used after every OTA or esptool flash to confirm firmware
is running correctly and all selftest checks pass.

Transport: TCP console 10.0.0.244:4242

Usage:
  python3 scripts/health_check.py
"""
import socket
import sys
import time

TCC_IP = '10.0.0.244'


def connect() -> socket.socket:
    s = socket.socket()
    s.settimeout(10)
    s.connect((TCC_IP, 4242))
    time.sleep(0.3)
    _drain(s)
    return s


def _drain(s: socket.socket, wait: float = 0.3) -> str:
    time.sleep(wait)
    buf = b''
    s.settimeout(0.2)
    try:
        while True:
            buf += s.recv(4096)
    except socket.timeout:
        pass
    s.settimeout(10)
    return buf.decode('utf-8', errors='replace')


def cmd(s: socket.socket, c: str, wait: float = 2.0) -> str:
    s.sendall((c + '\n').encode())
    return _drain(s, wait)


def main() -> None:
    print('Connecting to TCP console ...')
    s = connect()

    print('=== OTA STATUS ===')
    out = cmd(s, 'ota status', 2)
    print(out)

    print('=== SELFTEST ALL ===')
    out = cmd(s, 'selftest all', 15)
    print(out)

    # Summarize pass/fail
    lines  = out.splitlines()
    passed = sum(1 for l in lines if 'PASS' in l.upper() and 'FAIL' not in l.upper())
    failed = sum(1 for l in lines if 'FAIL' in l.upper())

    print(f'\n--- Summary: {passed} PASS  {failed} FAIL ---')
    if failed:
        print('HEALTH CHECK: FAIL')
        s.close()
        sys.exit(1)
    else:
        print('HEALTH CHECK: PASS')

    s.close()


if __name__ == '__main__':
    main()
