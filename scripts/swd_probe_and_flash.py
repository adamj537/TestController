#!/usr/bin/env python3
"""
swd_probe_and_flash.py — Assert PB-A, gate on swd probe, then flash DUT from partition.

Gate pattern: probe must PASS before flashing. Uses 'nopwrcycle' flag so
the DUT stays powered between probe and flash (avoids re-latch overhead).

Issue: S8 — clean form of the probe+flash workflow after GPIO45 hardware
mod and pyocd vs TCC swd flash re-litigation.

Transport: TCP console 10.0.0.244:4242

Usage:
  python3 scripts/swd_probe_and_flash.py
  python3 scripts/swd_probe_and_flash.py --no-pba   # skip PB-A (DUT already latched)
  python3 scripts/swd_probe_and_flash.py --url http://192.168.50.19:8081/firmware.bin
"""
import argparse
import socket
import sys
import time

TCC_IP       = '10.0.0.244'
FLASH_TIMEOUT = 300  # seconds — generous for erase+write+verify


def connect() -> socket.socket:
    s = socket.socket()
    s.settimeout(15)
    s.connect((TCC_IP, 4242))
    # drain banner
    try:
        while True:
            s.settimeout(2)
            s.recv(4096)
    except socket.timeout:
        pass
    s.settimeout(15)
    return s


def tcp_cmd(s: socket.socket, c: str, timeout: float = 5.0) -> str:
    s.sendall((c + '\n').encode())
    buf = ''
    deadline = time.time() + timeout
    while time.time() < deadline:
        try:
            s.settimeout(max(0.5, deadline - time.time()))
            d = s.recv(4096)
            if not d:
                break
            buf += d.decode('utf-8', errors='replace')
            # Stop at next prompt line (after the command echo)
            if 'g3-tc|' in buf.split('\n')[-1]:
                break
        except socket.timeout:
            break
    return buf


def main(assert_pba: bool = True, flash_url: str | None = None) -> None:
    s = connect()

    if assert_pba:
        # Configure U8 MUX outputs and assert PB-A
        mux_pins = [17, 18, 21, 35, 36]
        for pin in mux_pins:
            tcp_cmd(s, f'gpio mode {pin} out', timeout=2)
            tcp_cmd(s, f'gpio set {pin} 0',    timeout=2)
        print('PB-A asserted — settling 2s ...')
        time.sleep(2)

    # Gate: probe must pass
    print('swd probe ...')
    result = tcp_cmd(s, 'swd probe', timeout=10)
    print(result.strip())
    if 'PASS' not in result.upper():
        print('ERROR: swd probe failed — DUT not responding')
        s.close()
        sys.exit(1)

    # Flash
    if flash_url:
        flash_cmd = f'swd flash {flash_url} nopwrcycle'
    else:
        flash_cmd = 'swd flash local --target pfw nopwrcycle'

    print(f'\n=== {flash_cmd} ===')
    s.sendall((flash_cmd + '\n').encode())

    buf = b''
    start = time.time()
    while time.time() - start < FLASH_TIMEOUT:
        try:
            s.settimeout(5)
            d = s.recv(4096)
            if not d:
                print(f'[EOF at {time.time()-start:.1f}s]')
                break
            buf += d
            text = d.decode('utf-8', errors='replace')
            t = time.time() - start
            print(f'[{t:6.1f}s] {text}', end='', flush=True)
            if b'[PASS]' in buf or b'[FAIL]' in buf:
                break
        except socket.timeout:
            t = time.time() - start
            if t > 10:
                print(f'  [{t:.0f}s] waiting ...')
        except ConnectionResetError:
            print(f'[connection reset at {time.time()-start:.1f}s]')
            break

    elapsed = time.time() - start
    print(f'\n[total: {elapsed:.1f}s]')

    if b'[PASS]' in buf:
        print('SWD FLASH: PASS')
    elif b'[FAIL]' in buf:
        print('SWD FLASH: FAIL')
        s.close()
        sys.exit(1)
    else:
        print('SWD FLASH: no PASS/FAIL — check output above')

    s.close()


if __name__ == '__main__':
    p = argparse.ArgumentParser(description='swd probe gate + flash DUT from partition')
    p.add_argument('--no-pba', action='store_true',
                   help='Skip PB-A assertion (DUT already powered and latched)')
    p.add_argument('--url', default=None,
                   help='Flash from URL instead of stored partition')
    args = p.parse_args()
    main(assert_pba=not args.no_pba, flash_url=args.url)
