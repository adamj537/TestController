#!/usr/bin/env python3
"""
wifi_provision.py — Provision WiFi credentials via USB CDC console.

Issue: S6 — After NVS wipe (NordVPN interference with WPA3-SAE), TCP console
is unavailable; WiFi must be provisioned via CDC serial (/dev/ttyACM0).

Also use on any fresh flash when WiFi hasn't connected yet.

Transport: USB CDC serial (/dev/ttyACM0)

Usage:
  python3 scripts/wifi_provision.py
  python3 scripts/wifi_provision.py --port /dev/ttyUSB0 --ssid MyNet --pw secret
"""
import serial
import sys
import time

SSID        = 'CatsAndDogs2'
PASSWORD    = 'FADCADBAD1'
PORT        = '/dev/ttyACM0'
BAUD        = 115200
BOOT_WAIT   = 12   # seconds to wait for boot + prompt


def main(port: str = PORT, ssid: str = SSID, password: str = PASSWORD) -> None:
    print(f'Opening {port} at {BAUD} baud ...')
    s = serial.Serial(port, BAUD, timeout=1)
    time.sleep(0.5)

    # Capture boot log and wait for prompt
    print(f'Waiting up to {BOOT_WAIT}s for prompt ...')
    buf = b''
    deadline = time.time() + BOOT_WAIT
    while time.time() < deadline:
        chunk = s.read(512)
        if chunk:
            buf += chunk
            sys.stdout.write(chunk.decode('utf-8', errors='replace'))
            sys.stdout.flush()
            if b'> ' in buf or b'esp>' in buf:
                break

    print(f'\n--- Sending: wifi connect {ssid} ***')
    s.write(f'wifi connect {ssid} {password}\n'.encode())
    s.flush()
    time.sleep(10)

    out = s.read(s.in_waiting or 4096)
    response = out.decode('utf-8', errors='replace')
    print(response)

    if 'connected' in response.lower() or 'sta ip' in response.lower():
        print('WiFi provisioning: SUCCESS')
    else:
        print('WiFi provisioning: response unclear — check output above')

    s.close()


if __name__ == '__main__':
    import argparse
    p = argparse.ArgumentParser(description='Provision WiFi via CDC console')
    p.add_argument('--port', default=PORT)
    p.add_argument('--ssid', default=SSID)
    p.add_argument('--pw',   default=PASSWORD, dest='password')
    args = p.parse_args()
    main(port=args.port, ssid=args.ssid, password=args.password)
