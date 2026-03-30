#!/usr/bin/env python3
"""
swd_store_pfw.py — Store firmware to TC flash partition via 'swd flash --store'.

Serves a firmware binary over HTTP, issues
  swd flash <url> --store --target <pfw|prod>
then optionally verifies by re-flashing from the stored partition.

Issue: S7 — 'swd flash --store' workflow for persisting PFW so that
'swd flash local --target pfw' works without network access.

Transport: TCP console (TC_IP from tc_config):4242 + HTTP server

Usage:
  python3 scripts/swd_store_pfw.py [--fw path/to/firmware.bin] [--target pfw|prod] [--no-verify]
"""
import argparse
import os
import socket
import subprocess
import sys
import time

import os as _os, sys as _sys
_sys.path.insert(0, _os.path.dirname(__file__))
from tc_config import TC_IP as TCC_IP

HTTP_PORT   = 8080
DEFAULT_FW  = 'embedded/dut-firmware/.pio/build/g3-dut/firmware.bin'
CMD_TIMEOUT = 60            # seconds per swd command


def get_wsl_ip() -> str:
    """Return WSL IP on the same subnet as the TC (192.168.50.x)."""
    try:
        r = subprocess.run(['hostname', '-I'], capture_output=True, text=True)
        for ip in r.stdout.strip().split():
            if ip.startswith('192.168.50.'):
                return ip
        return r.stdout.strip().split()[0]
    except Exception:
        return '192.168.50.41'


def tc_cmd(cmd_str: str, timeout: float = CMD_TIMEOUT) -> str:
    """Open fresh TCP connection, send command, collect until prompt."""
    s = socket.socket()
    s.settimeout(timeout)
    s.connect((TCC_IP, 4242))
    time.sleep(0.2)
    try:
        s.settimeout(1)
        while True:
            s.recv(512)
    except socket.timeout:
        pass

    s.sendall((cmd_str + '\r\n').encode())
    buf = ''
    start = time.time()
    while time.time() - start < timeout:
        try:
            s.settimeout(max(1.0, timeout - (time.time() - start)))
            chunk = s.recv(1024)
            if not chunk:
                break
            buf += chunk.decode('utf-8', errors='replace')
            # Stop when we see the shell prompt after the command
            lines = buf.split(cmd_str)
            if len(lines) > 1 and 'g3-tc|' in lines[-1]:
                break
        except socket.timeout:
            break
    s.close()
    return buf


def main(fw_path: str = DEFAULT_FW, target: str = 'pfw',
         verify: bool = True) -> None:
    if not os.path.isfile(fw_path):
        print(f'ERROR: firmware not found: {fw_path}')
        sys.exit(1)

    fw_dir  = os.path.dirname(os.path.abspath(fw_path))
    fw_file = os.path.basename(fw_path)
    wsl_ip  = get_wsl_ip()
    url     = f'http://{wsl_ip}:{HTTP_PORT}/{fw_file}'

    part_name = 'dut_fw' if target == 'pfw' else 'prod_fw'
    print(f'Firmware: {fw_path}  ({os.path.getsize(fw_path)} bytes)')
    print(f'Target:   --target {target}  ({part_name} partition)')
    print(f'URL:      {url}')

    srv = subprocess.Popen(
        ['python3', '-m', 'http.server', str(HTTP_PORT)],
        cwd=fw_dir, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL,
    )
    time.sleep(1)

    try:
        print(f'\n=== Storing firmware to {part_name} partition ===')
        result = tc_cmd(f'swd flash {url} --store --target {target}', timeout=60)
        print(result[:800])

        if verify:
            print(f'\n=== Verifying: flash from stored {part_name} partition ===')
            result = tc_cmd(f'swd flash local --target {target}', timeout=60)
            print(result[:800])
            if '[PASS]' in result:
                print('\nSTORE + VERIFY: PASS')
            else:
                print('\nSTORE + VERIFY: FAIL or unclear')
                sys.exit(1)
    finally:
        srv.terminate()


if __name__ == '__main__':
    p = argparse.ArgumentParser(description='Store firmware to TC flash partition')
    p.add_argument('--fw',        default=DEFAULT_FW)
    p.add_argument('--target',    default='pfw', choices=['pfw', 'prod'],
                   help='Target partition: pfw (dut_fw) or prod (prod_fw)')
    p.add_argument('--no-verify', action='store_true',
                   help='Skip re-flash verification step')
    args = p.parse_args()
    main(fw_path=args.fw, target=args.target, verify=not args.no_verify)
