#!/usr/bin/env python3
"""
scan_both_buses.py — Scan both I2C bus orientations via UART serial console.

Issue: S1 — ADC128D818 SDA/SCL physically swapped on PCB; INA219 on default bus,
ADC128 on swapped bus. Run after any I2C regression to confirm bus layout.

Transport: UART serial (/dev/ttyUSB0)
Expected devices:
  Default  (SDA=GPIO15 SCL=GPIO16): INA219 @0x40, @0x41
  Swapped  (SDA=GPIO16 SCL=GPIO15): ADC128D818 @0x1D
"""
import serial, time

PORT = '/dev/ttyUSB0'
BAUD = 115200


def cmd(s, line, wait=0.4):
    s.write((line + '\r\n').encode())
    time.sleep(wait)
    out = b''
    while s.in_waiting:
        out += s.read(s.in_waiting)
        time.sleep(0.05)
    return out.decode('utf-8', errors='replace').strip()


s = serial.Serial(PORT, BAUD, timeout=1)
time.sleep(1)
s.read(s.in_waiting)  # drain banner

for sda, scl in [(15, 16), (16, 15)]:
    print(f'\n=== SDA=GPIO{sda}  SCL=GPIO{scl} ===')
    print(cmd(s, f'i2c init {sda} {scl}'))
    print(cmd(s, 'i2c scan', wait=3.0))

s.close()
