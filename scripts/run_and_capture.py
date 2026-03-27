#!/usr/bin/env python3
"""Send 'sm start' and capture full test run until result is published.

Reads serial output continuously until the TC publishes a result (detected
via 'tc_mqtt: result' log line) or returns to Idle, then prints everything.

Usage:
    python3 scripts/run_and_capture.py [port] [timeout_seconds]

    port            Serial port (default: /dev/ttyACM0)
    timeout_seconds Max seconds to wait for result (default: 300)
"""
import serial
import sys
import time


def main() -> int:
    port = sys.argv[1] if len(sys.argv) > 1 else "/dev/ttyACM0"
    timeout = float(sys.argv[2]) if len(sys.argv) > 2 else 300.0

    try:
        s = serial.Serial(port, 115200, timeout=0.1)
    except serial.SerialException as e:
        print(f"Cannot open {port}: {e}", file=sys.stderr)
        return 1

    # Flush pending data
    time.sleep(0.1)
    s.read(s.in_waiting or 1)
    time.sleep(0.1)

    # Send sm start
    s.write(b"sm start\r\n")
    print(">>> sm start", flush=True)

    buf = b""
    deadline = time.monotonic() + timeout
    done = False

    while time.monotonic() < deadline:
        chunk = s.read(1024)
        if chunk:
            buf += chunk
            text = chunk.decode("utf-8", errors="replace")
            print(text, end="", flush=True)
            # Stop when result is published or SM returns idle after Testing
            if b"tc_mqtt: result" in buf or b"tc_mqtt: publish_result" in buf:
                # Give a moment for any trailing log lines
                time.sleep(1.0)
                remaining = s.read(4096)
                if remaining:
                    print(remaining.decode("utf-8", errors="replace"), end="", flush=True)
                done = True
                break
            if b"Testing \xe2\x86\x92 Idle" in buf or b"Testing -> Idle" in buf or "Testing \u2192 Idle".encode() in buf:
                time.sleep(1.0)
                remaining = s.read(4096)
                if remaining:
                    print(remaining.decode("utf-8", errors="replace"), end="", flush=True)
                done = True
                break

    s.close()

    if not done:
        print(f"\n[run_and_capture] Timed out after {timeout}s — result not seen.", file=sys.stderr)
        return 1

    print("\n[run_and_capture] Done.", flush=True)
    return 0


if __name__ == "__main__":
    sys.exit(main())
