#!/usr/bin/env python3
"""Send a DCMD via MQTT and capture TC serial output to see the response.

Usage:
    dcmd_with_serial.py <command> [key=value ...] [--port /dev/ttyACM0] [--timeout 10]

Examples:
    dcmd_with_serial.py versions_get
    dcmd_with_serial.py selftest mode=fixture
    dcmd_with_serial.py rebirth --timeout 5
"""
import json
import serial
import sys
import threading
import time

import paho.mqtt.client as mqtt

BROKER = "10.0.0.178"
PORT = 1883
GROUP = "SensitMfg"
NODE = "G3-MB-Tester-000"
CHANNEL = 0
DCMD_TOPIC = f"spBv1.0/{GROUP}/DCMD/{NODE}/CH{CHANNEL}"


def send_dcmd(command: str, extra: dict[str, str]) -> None:
    """Publish JSON DCMD to TC."""
    payload = {"cmd": command}
    payload.update(extra)
    payload_json = json.dumps(payload)

    client = mqtt.Client()
    client.connect(BROKER, PORT, 5)
    result = client.publish(DCMD_TOPIC, payload_json.encode("utf-8"))
    result.wait_for_publish()
    time.sleep(0.3)
    print(f"[DCMD] Published: {payload_json}")
    client.disconnect()


def main() -> int:
    if len(sys.argv) < 2:
        print(__doc__)
        return 1

    # Parse args
    command = sys.argv[1]
    serial_port = "/dev/ttyACM0"
    timeout = 10.0
    extra: dict[str, str] = {}

    i = 2
    while i < len(sys.argv):
        arg = sys.argv[i]
        if arg == "--port" and i + 1 < len(sys.argv):
            serial_port = sys.argv[i + 1]
            i += 2
        elif arg == "--timeout" and i + 1 < len(sys.argv):
            timeout = float(sys.argv[i + 1])
            i += 2
        elif "=" in arg:
            k, v = arg.split("=", 1)
            extra[k] = v
            i += 1
        else:
            i += 1

    # Open serial port
    try:
        s = serial.Serial(serial_port, 115200, timeout=1)
    except serial.SerialException as e:
        print(f"Cannot open {serial_port}: {e}", file=sys.stderr)
        return 1

    # Flush pending data
    s.read(s.in_waiting or 1)
    time.sleep(0.1)

    # Send DCMD after 1s delay (let serial listener settle)
    dcmd_thread = threading.Thread(target=lambda: (time.sleep(1), send_dcmd(command, extra)))
    dcmd_thread.start()

    # Read serial output
    print(f"[SERIAL] Listening on {serial_port} for {timeout}s...")
    deadline = time.time() + timeout
    while time.time() < deadline:
        data = s.read(s.in_waiting or 1)
        if data:
            print(data.decode("utf-8", errors="replace"), end="")
        time.sleep(0.05)

    s.close()
    dcmd_thread.join()
    print("\n[DONE]")
    return 0


if __name__ == "__main__":
    sys.exit(main())
