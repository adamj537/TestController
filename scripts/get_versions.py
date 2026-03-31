#!/usr/bin/env python3
"""Send versions_get DCMD and print the Versions/* fields from the response."""

import json
import sys
import time
import paho.mqtt.client as mqtt

sys.path.insert(0, "scripts")
from tc_config import BROKER, TC_SERIAL

GROUP = "SensitMfg"
DCMD_TOPIC = f"spBv1.0/{GROUP}/DCMD/{TC_SERIAL}/CH0"
DDATA_TOPIC = f"spBv1.0/{GROUP}/DDATA/{TC_SERIAL}/CH0"

result: dict = {}
received = False

def on_connect(client, userdata, flags, rc):
    client.subscribe(DDATA_TOPIC)

def on_message(client, userdata, msg):
    global result, received
    try:
        d = json.loads(msg.payload.decode())
        if d.get("type") == "versions":
            result = d
            received = True
    except Exception:
        pass

client = mqtt.Client()
client.on_connect = on_connect
client.on_message = on_message
client.connect(BROKER, 1883, 60)
client.loop_start()
time.sleep(1)
client.publish(DCMD_TOPIC, json.dumps({"cmd": "versions_get"}))

for _ in range(80):
    if received:
        break
    time.sleep(0.1)

client.loop_stop()

if not result:
    print("No versions response received.")
    sys.exit(1)

for k, v in sorted(result.items()):
    if k.startswith("Versions/") or k == "type":
        print(f"{k}: {v}")
