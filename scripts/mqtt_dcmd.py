#!/usr/bin/env python3
"""Send a JSON DCMD to the TC via MQTT.

TC parses DCMDs as JSON with a "cmd" field, e.g. {"cmd":"rebirth"}.

Usage:
    mqtt_dcmd.py <command> [key=value ...]

Examples:
    mqtt_dcmd.py rebirth
    mqtt_dcmd.py versions_get
    mqtt_dcmd.py recipe_select id=g3-mb-v2
    mqtt_dcmd.py selftest mode=fixture
    mqtt_dcmd.py config_get
"""
import json
import sys
import time

import paho.mqtt.client as mqtt

BROKER = "10.0.0.178"
PORT = 1883
GROUP = "SensitMfg"
NODE = "G3-MB-Tester-000"
CHANNEL = 0

DCMD_TOPIC = f"spBv1.0/{GROUP}/DCMD/{NODE}/CH{CHANNEL}"


def main() -> int:
    if len(sys.argv) < 2:
        print(__doc__)
        return 1

    command = sys.argv[1]
    payload_dict: dict[str, str] = {"cmd": command}

    # Parse key=value pairs from remaining args
    for arg in sys.argv[2:]:
        if "=" in arg:
            k, v = arg.split("=", 1)
            payload_dict[k] = v
        else:
            # Positional arg — use as first unnamed param
            payload_dict["id"] = arg

    payload_json = json.dumps(payload_dict)

    client = mqtt.Client()
    client.connect(BROKER, PORT, 5)
    result = client.publish(DCMD_TOPIC, payload_json.encode("utf-8"))
    result.wait_for_publish()
    time.sleep(0.5)
    print(f"Published DCMD: {payload_json}")
    print(f"  Topic: {DCMD_TOPIC}")
    client.disconnect()
    return 0


if __name__ == "__main__":
    sys.exit(main())
