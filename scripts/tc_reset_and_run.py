#!/usr/bin/env python3
"""tc_reset_and_run.py — Abort stuck state machine, select recipe, and run.

Usage:
  python3 scripts/tc_reset_and_run.py [--tc-ip 192.168.50.30] [--recipe g3-mb-v2]
"""
import argparse
import socket
import time

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
    p = argparse.ArgumentParser(description='Abort TC state machine and run recipe')
    p.add_argument('--tc-ip', default=TC_IP)
    p.add_argument('--recipe', default='g3-mb-v2', help='Recipe name to select and run')
    p.add_argument('--select-only', action='store_true', help='Select recipe but do not run')
    args = p.parse_args()

    s = connect(args.tc_ip)

    print('=== sm status ===')
    r = cmd(s, 'sm status', 2.0)
    print(r.strip())

    print('\n=== sm abort ===')
    r = cmd(s, 'sm abort', 3.0)
    print(r.strip())
    time.sleep(1.0)

    print('\n=== vdac off ===')
    r = cmd(s, 'vdac off', 2.0)
    print(r.strip())

    print('\n=== recipe list ===')
    r = cmd(s, 'recipe list', 2.0)
    print(r.strip())

    print(f'\n=== recipe select {args.recipe} ===')
    r = cmd(s, f'recipe select {args.recipe}', 2.0)
    print(r.strip())

    print('\n=== sm status (after select) ===')
    r = cmd(s, 'sm status', 2.0)
    print(r.strip())

    if not args.select_only:
        print(f'\n=== sm start ===')
        r = cmd(s, 'sm start', 5.0)
        print(r.strip())

    s.close()
    print('\nDone.')


if __name__ == '__main__':
    main()
