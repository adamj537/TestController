#!/usr/bin/env python3
"""
swd_flash_dut.py — Flash DUT PFW via TCC 'swd flash' command.

Serves DUT firmware over HTTP, sends 'swd flash <url> --store' to the TC
via TCP console, and monitors for [PASS]/[FAIL].  The TC firmware handles
PB-A assertion, VDUT power cycling, and SWD programming internally.

Transport: TCP console 192.168.50.30:4242 + HTTP server (WSL → ESP32)

Usage:
  python3 scripts/swd_flash_dut.py [--fw path/to/firmware.bin]
"""
import argparse
import os
import socket
import subprocess
import sys
import time

TCC_IP       = '192.168.50.30'
HTTP_PORT    = 8080
DEFAULT_FW   = 'embedded/dut-firmware/.pio/build/g3-dut/firmware.bin'
FLASH_TIMEOUT = 120  # seconds


def get_wsl_ip() -> str:
    """Return WSL IP on the same subnet as the TC (192.168.50.x)."""
    try:
        import subprocess
        r = subprocess.run(['hostname', '-I'], capture_output=True, text=True)
        ips = r.stdout.strip().split()
        # prefer same subnet as TC (192.168.50.x)
        for ip in ips:
            if ip.startswith('192.168.50.'):
                return ip
        return ips[0] if ips else TCC_IP
    except Exception:
        return TCC_IP


def connect() -> socket.socket:
    s = socket.socket()
    s.settimeout(5)
    s.connect((TCC_IP, 4242))
    time.sleep(0.3)
    _drain(s)
    return s


def _drain(s: socket.socket, wait: float = 0.3) -> str:
    time.sleep(wait)
    buf = b''
    s.settimeout(0.1)
    try:
        while True:
            buf += s.recv(4096)
    except socket.timeout:
        pass
    s.settimeout(5)
    return buf.decode('utf-8', errors='replace')


def cmd(s: socket.socket, c: str, wait: float = 0.3) -> str:
    s.sendall((c + '\n').encode())
    return _drain(s, wait)


def main(fw_path: str = DEFAULT_FW) -> None:
    if not os.path.isfile(fw_path):
        print(f'ERROR: firmware not found: {fw_path}')
        print('Build with: pio run -e g3-dut  (in embedded/dut-firmware/)')
        sys.exit(1)

    fw_dir = os.path.dirname(os.path.abspath(fw_path))
    fw_file = os.path.basename(fw_path)
    wsl_ip = get_wsl_ip()
    url = f'http://{wsl_ip}:{HTTP_PORT}/{fw_file}'

    print(f'Firmware: {fw_path}  ({os.path.getsize(fw_path)} bytes)')
    print(f'Serving:  {url}')

    # Start HTTP server
    srv = subprocess.Popen(
        ['python3', '-m', 'http.server', str(HTTP_PORT)],
        cwd=fw_dir,
        stdout=subprocess.DEVNULL,
        stderr=subprocess.DEVNULL,
    )
    time.sleep(1)

    try:
        s = connect()

        # TC firmware handles PB-A assertion + VDUT power cycling internally.
        # --store saves to dut_fw partition for future 'swd flash local' use.
        flash_cmd = f'swd flash {url} --store'
        print(f'Sending: {flash_cmd}')
        s.settimeout(FLASH_TIMEOUT)
        s.sendall(f'{flash_cmd}\n'.encode())

        buf = b''
        start = time.time()
        while time.time() - start < FLASH_TIMEOUT:
            try:
                s.settimeout(5)
                chunk = s.recv(4096)
                if not chunk:
                    break
                buf += chunk
                sys.stdout.write(chunk.decode('utf-8', errors='replace'))
                sys.stdout.flush()
                if b'[PASS]' in buf or b'[FAIL]' in buf:
                    break
            except socket.timeout:
                continue

        elapsed = time.time() - start
        print(f'\n[{elapsed:.1f}s]')

        if b'[PASS]' in buf:
            print('SWD FLASH: PASS')
        elif b'[FAIL]' in buf:
            print('SWD FLASH: FAIL')
            s.close()
            sys.exit(1)
        else:
            print('SWD FLASH: no PASS/FAIL detected — check output')

        s.close()

    finally:
        srv.terminate()


if __name__ == '__main__':
    p = argparse.ArgumentParser(description='Flash DUT PFW via TCC swd flash')
    p.add_argument('--fw', default=DEFAULT_FW, help='Path to DUT firmware.bin')
    args = p.parse_args()
    main(fw_path=args.fw)
