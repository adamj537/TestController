#!/usr/bin/env python3
"""tc_cal_restore.py — Restore known-good VDUT1 calibration for TC-001.

Usage:
  python3 scripts/tc_cal_restore.py [--tc-ip 192.168.50.30] \
      --ch 0 --slope -87 --intercept 10811

Reads current cal, applies new VDUT slope/intercept, saves to NVS.
"""
import argparse
import socket
import time
import sys

TC_IP = '192.168.50.30'
TC_PORT = 4242


def connect(ip: str) -> socket.socket:
    s = socket.socket()
    s.settimeout(5)
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
    p = argparse.ArgumentParser(description='Restore TC VDUT1 calibration')
    p.add_argument('--tc-ip', default=TC_IP)
    p.add_argument('--ch', type=int, default=0, help='VDUT channel (0 or 1)')
    p.add_argument('--slope', type=int, required=True, help='slope_mv_per_pct (e.g. -87)')
    p.add_argument('--intercept', type=int, required=True, help='intercept_mv (e.g. 10811)')
    p.add_argument('--dry-run', action='store_true', help='Show commands but do not save')
    args = p.parse_args()

    s = connect(args.tc_ip)

    print('=== current cal ===')
    print(cmd(s, 'cal show', 2.0))

    set_cmd = f'cal vdut {args.ch} {args.slope} {args.intercept}'
    print(f'Applying: {set_cmd}')
    r = cmd(s, set_cmd, 2.0)
    print(r)

    verify_cmd = f'cal vdut-duty {args.ch} 3300'
    r = cmd(s, verify_cmd, 2.0)
    print(f'Verify duty@3300mV: {r.strip()}')

    if args.dry_run:
        print('Dry run — NOT saving.')
        s.close()
        sys.exit(0)

    r = cmd(s, 'cal save', 2.0)
    print(f'cal save: {r.strip()}')

    print('\n=== updated cal ===')
    print(cmd(s, 'cal show', 2.0))

    s.close()
    print('Done.')


if __name__ == '__main__':
    main()
