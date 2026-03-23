#!/usr/bin/env python3
"""Send a command to TC serial console and print the response.

Usage:
    serial_cmd.py <command> [port] [timeout_seconds]

Examples:
    serial_cmd.py "mqtt status"
    serial_cmd.py "dut status" /dev/ttyACM0 3
    serial_cmd.py "recipe list"
"""
import serial
import sys
import time


def main() -> int:
    if len(sys.argv) < 2:
        print(__doc__)
        return 1

    cmd = sys.argv[1]
    port = sys.argv[2] if len(sys.argv) > 2 else "/dev/ttyACM0"
    timeout = float(sys.argv[3]) if len(sys.argv) > 3 else 3.0

    try:
        s = serial.Serial(port, 115200, timeout=timeout)
    except serial.SerialException as e:
        print(f"Cannot open {port}: {e}", file=sys.stderr)
        return 1

    # Flush any pending data
    s.read(s.in_waiting or 1)
    time.sleep(0.1)

    # Send command
    s.write(f"{cmd}\r\n".encode())
    time.sleep(timeout)

    # Read response
    data = s.read(4096)
    print(data.decode("utf-8", errors="replace"))

    s.close()
    return 0


if __name__ == "__main__":
    sys.exit(main())
