#!/usr/bin/env python3
"""Send a command to TC serial console and print the response.

Usage:
    serial_cmd.py <command> [port] [timeout_seconds]
    serial_cmd.py --reboot [port] [--watch seconds]

Examples:
    serial_cmd.py "mqtt status"
    serial_cmd.py "dut status" /dev/ttyACM0 3
    serial_cmd.py "recipe list"
    serial_cmd.py --reboot                        # reset via DTR/RTS toggle
    serial_cmd.py --reboot /dev/ttyACM0 --watch 15  # reset + capture boot log
"""
import serial
import sys
import time


def reboot(port: str, watch: int) -> int:
    """Reset ESP32-S3 via USB-Serial-JTAG DTR/RTS toggle."""
    try:
        s = serial.Serial(port, 115200, timeout=1)
    except serial.SerialException as e:
        print(f"Cannot open {port}: {e}", file=sys.stderr)
        return 1

    print(f"Resetting via {port} ...")
    s.dtr = False
    s.rts = True
    time.sleep(0.1)
    s.dtr = True
    s.rts = False
    time.sleep(0.1)
    s.dtr = False
    print("Reset pulse sent.")

    if watch > 0:
        print(f"Watching boot output for {watch}s ...")
        deadline = time.time() + watch
        while time.time() < deadline:
            chunk = s.read(512)
            if chunk:
                sys.stdout.write(chunk.decode("utf-8", errors="replace"))
                sys.stdout.flush()

    s.close()
    return 0


def send_cmd(cmd: str, port: str, timeout: float) -> int:
    """Send a command and print the response."""
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


def main() -> int:
    if len(sys.argv) < 2:
        print(__doc__)
        return 1

    if sys.argv[1] == "--reboot":
        port = "/dev/ttyACM0"
        watch = 0
        args = sys.argv[2:]
        while args:
            if args[0] == "--watch":
                watch = int(args[1]) if len(args) > 1 else 15
                args = args[2:]
            elif not args[0].startswith("-"):
                port = args[0]
                args = args[1:]
            else:
                args = args[1:]
        return reboot(port, watch)

    cmd = sys.argv[1]
    port = sys.argv[2] if len(sys.argv) > 2 else "/dev/ttyACM0"
    timeout = float(sys.argv[3]) if len(sys.argv) > 3 else 3.0

    return send_cmd(cmd, port, timeout)


if __name__ == "__main__":
    sys.exit(main())
