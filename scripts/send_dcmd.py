#!/usr/bin/env python3
"""Send a JSON DCMD to the TC via MQTT.

Usage:
    python3 scripts/send_dcmd.py rebirth
    python3 scripts/send_dcmd.py reset
    python3 scripts/send_dcmd.py recipe_select recipe_id=g3-mb-v2
    python3 scripts/send_dcmd.py start recipe_id=g3-mb-v2 operation_id=OP-001
"""

import json
import sys
import time

try:
    import paho.mqtt.client as mqtt
except ImportError:
    print("paho-mqtt required: pip install paho-mqtt")
    sys.exit(1)

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
    payload: dict = {"cmd": command}

    for arg in sys.argv[2:]:
        if "=" in arg:
            k, v = arg.split("=", 1)
            payload[k] = v
        else:
            payload["id"] = arg

    payload_json = json.dumps(payload)

    client = mqtt.Client()
    client.connect(BROKER, PORT, 5)
    result = client.publish(DCMD_TOPIC, payload_json.encode("utf-8"), qos=1)
    result.wait_for_publish()
    time.sleep(0.5)
    print(f"Published: {payload_json}")
    print(f"  Topic: {DCMD_TOPIC}")
    client.disconnect()
    return 0


if __name__ == "__main__":
    sys.exit(main())
