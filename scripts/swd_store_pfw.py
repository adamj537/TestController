#!/usr/bin/env python3
"""
swd_store_pfw.py — Store DUT PFW to the 'dut_fw' flash partition.

Serves PFW binary over HTTP, issues 'swd flash <url> --store --target pfw',
then optionally verifies by re-flashing from the stored partition.

Issue: S7 — 'swd flash --store' workflow for persisting PFW so that
'swd flash local --target pfw' works without network access.

Transport: TCP console 10.0.0.244:4242 + HTTP server

Usage:
  python3 scripts/swd_store_pfw.py [--fw path/to/firmware.bin] [--no-verify]
"""
import argparse
import os
import socket
import subprocess
import sys
import time

TCC_IP      = '10.0.0.244'
HTTP_PORT   = 8081          # use 8081 to avoid conflict with TCC OTA server
DEFAULT_FW  = 'embedded/dut-firmware/.pio/build/g3-dut/firmware.bin'
CMD_TIMEOUT = 60            # seconds per swd command


def get_wsl_ip() -> str:
    try:
        r = subprocess.run(['hostname', '-I'], capture_output=True, text=True)
        for ip in r.stdout.strip().split():
            if ip.startswith('10.'):
                return ip
        return r.stdout.strip().split()[0]
    except Exception:
        return '10.0.0.139'


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


def main(fw_path: str = DEFAULT_FW, verify: bool = True) -> None:
    if not os.path.isfile(fw_path):
        print(f'ERROR: firmware not found: {fw_path}')
        sys.exit(1)

    fw_dir  = os.path.dirname(os.path.abspath(fw_path))
    fw_file = os.path.basename(fw_path)
    wsl_ip  = get_wsl_ip()
    url     = f'http://{wsl_ip}:{HTTP_PORT}/{fw_file}'

    print(f'Firmware: {fw_path}  ({os.path.getsize(fw_path)} bytes)')
    print(f'URL:      {url}')

    srv = subprocess.Popen(
        ['python3', '-m', 'http.server', str(HTTP_PORT)],
        cwd=fw_dir, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL,
    )
    time.sleep(1)

    try:
        print('\n=== Storing PFW to dut_fw partition ===')
        result = tc_cmd(f'swd flash {url} --store --target pfw', timeout=60)
        print(result[:800])

        if verify:
            print('\n=== Verifying: flash from stored partition ===')
            result = tc_cmd('swd flash local --target pfw', timeout=60)
            print(result[:800])
            if '[PASS]' in result:
                print('\nSTORE + VERIFY: PASS')
            else:
                print('\nSTORE + VERIFY: FAIL or unclear')
                sys.exit(1)
    finally:
        srv.terminate()


if __name__ == '__main__':
    p = argparse.ArgumentParser(description='Store DUT PFW to dut_fw partition')
    p.add_argument('--fw',        default=DEFAULT_FW)
    p.add_argument('--no-verify', action='store_true',
                   help='Skip re-flash verification step')
    args = p.parse_args()
    main(fw_path=args.fw, verify=not args.no_verify)
