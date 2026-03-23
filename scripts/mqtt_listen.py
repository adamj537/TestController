#!/usr/bin/env python3
"""Listen for MQTT messages from the TC and print them.

Usage:
    mqtt_listen.py [topic_filter] [timeout_seconds]

Defaults:
    topic_filter = spBv1.0/SensitG3/DDATA/#
    timeout = 10
"""
import json
import sys
import time

import paho.mqtt.client as mqtt

BROKER = "10.0.0.178"
PORT = 1883


def main() -> int:
    topic = sys.argv[1] if len(sys.argv) > 1 else "spBv1.0/SensitMfg/DDATA/#"
    timeout = int(sys.argv[2]) if len(sys.argv) > 2 else 10

    msgs: list[tuple[str, str]] = []

    def on_message(client: mqtt.Client, userdata: None, msg: mqtt.MQTTMessage) -> None:
        payload = msg.payload.decode("utf-8", errors="replace")
        msgs.append((msg.topic, payload))
        # Pretty-print JSON if possible
        try:
            obj = json.loads(payload)
            print(f"Topic: {msg.topic}")
            print(json.dumps(obj, indent=2))
        except json.JSONDecodeError:
            print(f"Topic: {msg.topic}")
            print(f"Payload: {payload[:500]}")
        print()

    client = mqtt.Client()
    client.on_message = on_message
    client.connect(BROKER, PORT, 5)
    client.subscribe(topic)

    deadline = time.time() + timeout
    while time.time() < deadline:
        client.loop(timeout=1.0)

    if not msgs:
        print(f"No messages received on {topic} within {timeout}s")

    client.disconnect()
    return 0


if __name__ == "__main__":
    sys.exit(main())
