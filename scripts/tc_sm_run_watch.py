#!/usr/bin/env python3
"""tc_sm_run_watch.py — Start SM and watch output until result or timeout.

Usage: python3 scripts/tc_sm_run_watch.py [--tc-ip 192.168.50.30] [--timeout 30]
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


def watch(s: socket.socket, wait: float = 30.0) -> str:
    """Read everything until idle prompt or timeout."""
    out = []
    deadline = time.time() + wait
    s.settimeout(1)
    while time.time() < deadline:
        try:
            chunk = s.recv(4096).decode('utf-8', errors='replace')
            if chunk:
                out.append(chunk)
                print(chunk, end='', flush=True)
                # Done when SM returns to idle prompt after a result
                if ('state: Idle' in chunk or 'state=Idle' in chunk or
                        'outcome=' in chunk or 'pre_gate_fail' in chunk or
                        'current_low' in chunk or 'vdut_uncalibrated' in chunk):
                    time.sleep(1.0)  # collect any trailing output
        except socket.timeout:
            continue
    return ''.join(out)


def main() -> None:
    p = argparse.ArgumentParser()
    p.add_argument('--tc-ip', default=TC_IP)
    p.add_argument('--timeout', type=float, default=30.0)
    args = p.parse_args()

    s = connect(args.tc_ip)

    cmd(s, 'sm abort', 2.0)
    cmd(s, 'vdac off', 2.0)
    time.sleep(0.5)

    print('=== sm start — watching output ===')
    cmd(s, 'sm start', 1.0)
    watch(s, args.timeout)

    s.close()
    print('\n\nDone.')


if __name__ == '__main__':
    main()
