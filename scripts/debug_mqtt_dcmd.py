#!/usr/bin/env python3
"""Debug MQTT DCMD delivery — subscribe to TC topics, send a rebirth, verify routing.

Step 1: Subscribe to DCMD topic (confirm our message loops back via broker)
Step 2: Subscribe to DDATA topic (confirm TC receives and responds)
Step 3: Send rebirth DCMD
Step 4: Report what was seen

Usage:
    python3 scripts/debug_mqtt_dcmd.py [cmd]
    python3 scripts/debug_mqtt_dcmd.py rebirth
    python3 scripts/debug_mqtt_dcmd.py versions_get
"""
import json
import os as _os
import sys
import time

import paho.mqtt.client as mqtt

sys.path.insert(0, _os.path.dirname(__file__))
from tc_config import BROKER, TC_SERIAL

PORT = 1883
CHANNEL = 0
GROUP = "SensitMfg"
NODE = TC_SERIAL

DCMD_TOPIC  = f"spBv1.0/{GROUP}/DCMD/{NODE}/CH{CHANNEL}"
NCMD_TOPIC  = f"spBv1.0/{GROUP}/NCMD/{NODE}"
DDATA_TOPIC = f"spBv1.0/{GROUP}/DDATA/{NODE}/CH{CHANNEL}"
NBIRTH_TOPIC = f"spBv1.0/{GROUP}/NBIRTH/{NODE}/CH{CHANNEL}"
ALL_TOPIC   = f"spBv1.0/{GROUP}/#"

cmd = sys.argv[1] if len(sys.argv) > 1 else "versions_get"
extra_args: dict[str, str] = {}
for arg in sys.argv[2:]:
    if "=" in arg:
        k, v = arg.split("=", 1)
        extra_args[k] = v

received: list[tuple[str, str]] = []
dcmd_loopback = False

def on_connect(client: mqtt.Client, userdata, flags, rc):  # type: ignore[override]
    print(f"  Broker connected (rc={rc})")

def on_message(client: mqtt.Client, userdata, msg: mqtt.MQTTMessage) -> None:
    global dcmd_loopback
    ts = time.time()
    payload = msg.payload.decode("utf-8", errors="replace")
    received.append((msg.topic, payload))
    short_payload = payload[:120].replace("\n", " ")
    print(f"  [{ts:.1f}] RECV {msg.topic}")
    print(f"          {short_payload}")
    if msg.topic == DCMD_TOPIC:
        dcmd_loopback = True

def main() -> int:
    print(f"Broker : {BROKER}:{PORT}")
    print(f"TC node: {NODE}/CH{CHANNEL}")
    print(f"DCMD   : {DCMD_TOPIC}")
    print()

    client = mqtt.Client()
    client.on_connect = on_connect
    client.on_message = on_message
    client.loop_start()

    print("[1] Connecting to broker...")
    try:
        client.connect(BROKER, PORT, 60)
    except Exception as e:
        print(f"  FAIL: {e}")
        client.loop_stop()
        return 1
    time.sleep(1)

    print(f"[2] Subscribing to all TC topics ({ALL_TOPIC})...")
    client.subscribe(ALL_TOPIC, qos=1)
    time.sleep(0.5)

    payload_dict: dict[str, object] = {"cmd": cmd, **extra_args}
    payload_str = json.dumps(payload_dict)
    print(f"[3] Publishing DCMD: {payload_str}")
    print(f"    → {DCMD_TOPIC}")
    result = client.publish(DCMD_TOPIC, payload_str.encode("utf-8"), qos=1)
    result.wait_for_publish()
    print(f"    publish rc={result.rc}  mid={result.mid}")
    print()

    print("[4] Waiting 8s for TC response...")
    time.sleep(8)

    client.loop_stop()
    client.disconnect()

    print()
    print("── Summary ──────────────────────────────────────────────────────────")
    if not received:
        print("  NO messages received from broker at all!")
        print("  → Broker may not be routing  OR  TC is not publishing NBIRTH/DDATA")
    else:
        print(f"  Received {len(received)} message(s):")
        for topic, payload in received:
            print(f"    {topic}")
    print()
    print(f"  DCMD loopback (broker echoed our own publish): {dcmd_loopback}")
    print("  If loopback=True but no TC response → TC subscription not active")
    print("  If loopback=False → broker routing issue")
    return 0


if __name__ == "__main__":
    sys.exit(main())
