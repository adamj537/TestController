#!/usr/bin/env python3
"""get_nbirth.py — Trigger MQTT rebirth and capture the NBIRTH payload.

Publishes Node Control/Rebirth to the TC, then waits for the NBIRTH response.
Prints key diagnostic fields: FirmwareVersion, FreeHeap, LastResetReason, etc.

Usage:
    python3 scripts/get_nbirth.py [--broker IP] [--serial G3-MB-Tester-000] [--timeout 10]
"""

import argparse
import json
import sys
import time
import os

import paho.mqtt.client as mqtt

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from tc_config import BROKER, TC_SERIAL

MQTT_PORT = 1883
GROUP     = "SensitMfg"


def main() -> None:
    p = argparse.ArgumentParser()
    p.add_argument("--broker",  default=BROKER)
    p.add_argument("--serial",  default=TC_SERIAL)
    p.add_argument("--timeout", type=float, default=10.0)
    args = p.parse_args()

    nbirth: list[dict] = []

    def on_connect(c: mqtt.Client, u: object, f: object, rc: object, props: object) -> None:
        topic = f"spBv1.0/{GROUP}/NBIRTH/{args.serial}/CH0"
        c.subscribe(topic, 1)
        print(f"[MQTT] subscribed → {topic}", flush=True)
        rebirth_topic = f"spBv1.0/{GROUP}/NCMD/{args.serial}"
        c.publish(rebirth_topic, json.dumps({"Node Control/Rebirth": True}))
        print(f"[MQTT] published rebirth → {rebirth_topic}", flush=True)

    def on_message(c: mqtt.Client, u: object, msg: mqtt.MQTTMessage) -> None:
        try:
            obj = json.loads(msg.payload.decode("utf-8", errors="replace"))
            nbirth.append(obj)
        except json.JSONDecodeError:
            pass

    client = mqtt.Client(mqtt.CallbackAPIVersion.VERSION2)
    client.on_connect = on_connect
    client.on_message = on_message

    try:
        client.connect(args.broker, MQTT_PORT, keepalive=30)
        client.loop_start()
        deadline = time.time() + args.timeout
        while time.time() < deadline and not nbirth:
            time.sleep(0.2)
        client.loop_stop()
        client.disconnect()
    except OSError as e:
        print(f"ERROR: cannot connect to broker at {args.broker}:{MQTT_PORT} — {e}", file=sys.stderr)
        sys.exit(1)

    if not nbirth:
        print(f"No NBIRTH received within {args.timeout}s")
        sys.exit(1)

    m = nbirth[0]
    print("\n--- NBIRTH fields ---")
    for key in [
        "Properties/FirmwareVersion",
        "Properties/LastResetReason",
        "Diagnostics/FreeHeap",
        "Diagnostics/WiFiRSSI",
        "Properties/SerialNumber",
        "Properties/FixtureId",
        "Properties/RecipeVersion",
        "Properties/CalProfileVersion",
        "State",
        "bdSeq",
    ]:
        val = m.get(key, "NOT PRESENT")
        print(f"  {key}: {val}")

    print("\n--- Full NBIRTH payload ---")
    print(json.dumps(m, indent=2))


if __name__ == "__main__":
    main()
