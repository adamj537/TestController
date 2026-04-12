#!/usr/bin/env python3
"""Passively listen on a serial port and print anything received.

Usage:
    serial_listen.py [port] [duration_seconds]

Examples:
    serial_listen.py /dev/ttyUSB0 10
    serial_listen.py /dev/ttyACM0 5
"""
import serial
import sys
import time


def listen(port: str, duration: float) -> int:
    try:
        s = serial.Serial(port, 115200, timeout=0.1)
    except serial.SerialException as e:
        print(f"Cannot open {port}: {e}", file=sys.stderr)
        return 1

    print(f"Listening on {port} for {duration}s ...", flush=True)
    deadline = time.time() + duration
    total_bytes = 0

    while time.time() < deadline:
        chunk = s.read(512)
        if chunk:
            total_bytes += len(chunk)
            sys.stdout.write(chunk.decode("utf-8", errors="replace"))
            sys.stdout.flush()

    s.close()
    print(f"\n--- {total_bytes} bytes received ---", flush=True)
    return 0


def main() -> int:
    port = sys.argv[1] if len(sys.argv) > 1 else "/dev/ttyUSB0"
    duration = float(sys.argv[2]) if len(sys.argv) > 2 else 10.0
    return listen(port, duration)


if __name__ == "__main__":
    sys.exit(main())
