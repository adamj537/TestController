#!/usr/bin/env python3
"""Query TC for stored firmware partition info via serial console.

Checks dut_fw and prod_fw partition headers by running 'swd info' console command.
"""
import serial
import sys
import time


def serial_cmd(port: str, cmd: str, timeout: float = 3.0) -> str:
    """Send a command to TC serial console and return response."""
    with serial.Serial(port, 115200, timeout=timeout) as s:
        s.write(f"{cmd}\r\n".encode())
        time.sleep(timeout)
        return s.read(4096).decode("utf-8", errors="replace")


def main() -> int:
    port = sys.argv[1] if len(sys.argv) > 1 else "/dev/ttyACM0"

    # Check swd info for stored firmware details
    print("=== swd info ===")
    resp = serial_cmd(port, "swd info")
    print(resp)

    # Also check swd flash --info if available
    print("=== swd flash pfw (info only) ===")
    resp = serial_cmd(port, "swd flash --info")
    print(resp)

    return 0


if __name__ == "__main__":
    sys.exit(main())
