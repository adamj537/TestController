#!/usr/bin/env python3
"""
capture_boot_log.py — Reset ESP32 via RTS and capture full boot log.

Filters for PSRAM, heap, WiFi, and firmware identification lines.
Detects boot loops (RTCWDT_RTC_RST).

Issue: S6/S7 — PSRAM/sdkconfig debugging; confirming QSPI mode after
OCT→QUAD switch; diagnosing SPIRAM_BOOT_HW_INIT boot loops.

Transport: UART serial (/dev/ttyUSB0 or /dev/ttyACM0)

Usage:
  python3 scripts/capture_boot_log.py                     # /dev/ttyUSB0, all lines
  python3 scripts/capture_boot_log.py --port /dev/ttyACM0
  python3 scripts/capture_boot_log.py --raw               # print all output
"""
import argparse
import serial
import sys
import time

PORT     = '/dev/ttyUSB0'
BAUD     = 115200
DURATION = 12  # seconds

FILTER_KEYWORDS = [
    'psram', 'spiram', 'ext_ram', 'g3-tc',
    'heap_init', 'sta ip', 'heap', 'malloc',
    'mode:', 'rst:', 'boot:', 'entry',
    'fw ', 'version',
]


def main(port: str = PORT, raw: bool = False) -> None:
    print(f'Opening {port} at {BAUD} baud ...')
    ser = serial.Serial(port, BAUD, timeout=0.5,
                        xonxoff=False, rtscts=False, dsrdtr=False)

    # Hardware reset via RTS
    ser.setRTS(True)
    time.sleep(0.1)
    ser.setRTS(False)
    time.sleep(0.05)

    print(f'Capturing boot log for {DURATION}s ...')
    start = time.time()
    output = b''
    while time.time() - start < DURATION:
        chunk = ser.read(4096)
        if chunk:
            output += chunk
        time.sleep(0.01)

    ser.close()
    text  = output.decode('utf-8', errors='replace')
    lines = text.split('\n')

    if raw:
        print(text)
        return

    # Boot loop detection
    if 'RTCWDT_RTC_RST' in text:
        parts = text.split('ESP-ROM')
        print('*** BOOT LOOP DETECTED — showing last attempt ***')
        print('ESP-ROM' + parts[-1][:3000] if len(parts) > 1 else text[:3000])
        return

    # Filtered output
    print(f'--- {len(lines)} lines, {len(text)} chars ---')
    for i, line in enumerate(lines):
        low = line.lower()
        if any(k in low for k in FILTER_KEYWORDS):
            print(f'L{i:4d}: {line.rstrip()}')

    print('--- end of filtered boot log ---')


if __name__ == '__main__':
    p = argparse.ArgumentParser(description='Capture ESP32 boot log via UART')
    p.add_argument('--port', default=PORT)
    p.add_argument('--raw',  action='store_true', help='Print all output unfiltered')
    args = p.parse_args()
    main(port=args.port, raw=args.raw)
