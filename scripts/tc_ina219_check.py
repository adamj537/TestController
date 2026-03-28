#!/usr/bin/env python3
"""tc_ina219_check.py — Read INA219 current with VDUT1 on/off.

Verifies DUT current draw at calibrated 3300mV.

Usage: python3 scripts/tc_ina219_check.py [--tc-ip 192.168.50.30]
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


def cmd(s: socket.socket, c: str, wait: float = 3.0) -> str:
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
    p = argparse.ArgumentParser(description='INA219 current check vs VDUT1')
    p.add_argument('--tc-ip', default=TC_IP)
    args = p.parse_args()

    s = connect(args.tc_ip)

    # VDUT off baseline
    cmd(s, 'vdac off', 2.0)
    time.sleep(0.5)
    r = cmd(s, 'selftest ina', 4.0)
    print('=== INA219 with VDUT off ===')
    print(r.strip())

    # Get calibrated duty for 3300mV
    r = cmd(s, 'cal vdut-duty 0 3300', 2.0)
    m = re.search(r'duty\s+(\d+)%', r)
    duty = int(m.group(1)) if m else 87
    print(f'\nCalibrated duty for 3300mV: {duty}%')

    # Apply VDUT1 manually
    r = cmd(s, f'vdac set 0 {duty}', 2.0)
    print(f'vdac set 0 {duty}: {r.strip()}')
    time.sleep(1.5)

    r = cmd(s, 'selftest ina', 4.0)
    print('\n=== INA219 with VDUT on ===')
    print(r.strip())

    cmd(s, 'vdac off', 1.0)
    s.close()
    print('\nDone.')


if __name__ == '__main__':
    main()
