#!/usr/bin/env python3
"""tc_ina219_reinit.py — Re-initialize INA219 CAL register and verify current reading.

selftest_ina219_init() is called at boot but can be overwritten during selftest
runs that reconfigure I2C peripherals.  This script uses 'selftest i2c' to
trigger a re-scan (which re-inits INA219) then lowers the pre_gate threshold
to 0 to let SM pass through to SWD UID — confirming DUT physical connectivity.

Usage: python3 scripts/tc_ina219_reinit.py [--tc-ip 192.168.50.30]
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


def watch(s: socket.socket, wait: float = 15.0) -> str:
    out = []
    deadline = time.time() + wait
    s.settimeout(1)
    while time.time() < deadline:
        try:
            chunk = s.recv(4096).decode('utf-8', errors='replace')
            if chunk:
                out.append(chunk)
                print(chunk, end='', flush=True)
                if 'state=Idle' in chunk or 'state: Idle' in chunk or 'Fail →' in chunk:
                    time.sleep(0.5)
                    break
        except socket.timeout:
            continue
    return ''.join(out)


def main() -> None:
    p = argparse.ArgumentParser()
    p.add_argument('--tc-ip', default=TC_IP)
    args = p.parse_args()

    s = connect(args.tc_ip)

    print('Step 1: run selftest i2c (re-inits INA219 CAL register)')
    r = cmd(s, 'selftest i2c', 8.0)
    print(r.strip())

    print('\nStep 2: lower pre_gate current_min_ma to 0 temporarily')
    r = cmd(s, 'config set pre_gate.current_min_ma 0', 2.0)
    print(r.strip())

    print('\nStep 3: sm start — watching for SWD UID result')
    cmd(s, 'sm abort', 2.0)
    cmd(s, 'vdac off', 2.0)
    time.sleep(0.3)
    cmd(s, 'sm start', 1.0)
    watch(s, 15.0)

    print('\n\nStep 4: restore current_min_ma to 5')
    r = cmd(s, 'config set pre_gate.current_min_ma 5', 2.0)
    print(r.strip())

    s.close()
    print('\nDone.')


if __name__ == '__main__':
    main()
