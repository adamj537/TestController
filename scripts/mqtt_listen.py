#!/usr/bin/env python3
"""Listen for MQTT messages from the TC and print them.

Usage:
    mqtt_listen.py [topic_filter] [timeout_seconds] [broker_ip]

Defaults:
    topic_filter = spBv1.0/SensitMfg/DDATA/#
    timeout = 10
    broker_ip = 10.0.0.59
"""
import json
import sys
import time

import paho.mqtt.client as mqtt

DEFAULT_BROKER = "10.0.0.59"
PORT = 1883


def main() -> int:
    topic = sys.argv[1] if len(sys.argv) > 1 else "spBv1.0/SensitMfg/DDATA/#"
    timeout = int(sys.argv[2]) if len(sys.argv) > 2 else 10
    broker = sys.argv[3] if len(sys.argv) > 3 else DEFAULT_BROKER

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

    print(f"Connecting to {broker}:{PORT}, topic={topic}, timeout={timeout}s")
    client = mqtt.Client()
    client.on_message = on_message
    client.connect(broker, PORT, 5)
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
