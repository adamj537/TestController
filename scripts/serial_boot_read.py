#!/usr/bin/env python3
"""
serial_boot_read.py — Reset ESP32 via DTR, capture boot log, send 'help'.

Issue: S1 — Initial ESP32-S3 bringup, UART console verification.
Transport: UART serial (/dev/ttyUSB0)
"""
import serial, time

PORT = '/dev/ttyUSB0'
BAUD = 115200

ser = serial.Serial(PORT, BAUD, timeout=2)
ser.dtr = False; time.sleep(0.1); ser.dtr = True
time.sleep(3)

out = b''
deadline = time.time() + 5
while time.time() < deadline:
    chunk = ser.read(ser.in_waiting or 1)
    if chunk:
        out += chunk

ser.write(b'help\r\n')
time.sleep(1)
out += ser.read(ser.in_waiting)
ser.close()
print(out.decode('utf-8', errors='replace'))
