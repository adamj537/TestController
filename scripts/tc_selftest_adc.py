#!/usr/bin/env python3
"""tc_selftest_adc.py — Run selftest adc to read INA219 with VDUT on.

Usage: python3 scripts/tc_selftest_adc.py [--tc-ip 192.168.50.30]
"""
import argparse
import socket
import time
import re

TC_IP = '192.168.50.30'
TC_PORT = 4242


def connect(ip: str) -> socket.socket:
    s = socket.socket()
    s.settimeout(10)
    s.connect((ip, TC_PORT))
    time.sleep(0.3)
    s.settimeout(0.5)
    try:
        while True:
            s.recv(4096)
    except socket.timeout:
        pass
    s.settimeout(10)
    return s


def cmd(s: socket.socket, c: str, wait: float = 5.0) -> str:
    s.sendall((c + '\n').encode())
    out = []
    deadline = time.time() + wait
    s.settimeout(1)
    while time.time() < deadline:
        try:
            chunk = s.recv(4096).decode('utf-8', errors='replace')
            if chunk:
                out.append(chunk)
                if 'g3-tc|' in chunk and '>' in chunk:
                    break
        except socket.timeout:
            continue
    return ''.join(out)


def main() -> None:
    p = argparse.ArgumentParser()
    p.add_argument('--tc-ip', default=TC_IP)
    args = p.parse_args()

    s = connect(args.tc_ip)

    print('=== selftest adc (VDUT off baseline) ===')
    r = cmd(s, 'selftest adc', 8.0)
    print(r.strip())

    print('\n=== selftest vdut (will power VDUT1 and measure) ===')
    r = cmd(s, 'selftest vdut', 15.0)
    print(r.strip())

    s.close()
    print('\nDone.')


if __name__ == '__main__':
    main()
