#!/usr/bin/env python3
"""Push a recipe JSON file to TC NVS via MQTT DCMD recipe_update.

Bypasses the TCP console 4KB line-length limit by using the MQTT DCMD path
which has a 16KB buffer.

Usage:
    python3 scripts/push_recipe_mqtt.py recipes/g3-mb-v2.json [--broker 10.0.0.59]
    python3 scripts/push_recipe_mqtt.py recipes/g3-mb-v2.json --set-active
"""

import argparse
import json
import os
import sys
import time

try:
    import paho.mqtt.client as mqtt
except ImportError:
    print("paho-mqtt required: pip install paho-mqtt")
    sys.exit(1)

BROKER = "10.0.0.59"
PORT = 1883
GROUP = "SensitMfg"
NODE = "G3-MB-Tester-000"
CHANNEL = 0


def main() -> int:
    parser = argparse.ArgumentParser(description="Push recipe JSON to TC via MQTT DCMD")
    parser.add_argument("recipe_file", help="Path to recipe JSON file")
    parser.add_argument("--broker", default=BROKER, help=f"MQTT broker (default {BROKER})")
    parser.add_argument("--port", default=PORT, type=int)
    parser.add_argument("--node", default=NODE, help=f"Sparkplug node ID (default {NODE})")
    parser.add_argument("--channel", default=CHANNEL, type=int, help=f"TC channel (default {CHANNEL})")
    parser.add_argument("--set-active", action="store_true",
                        help="Set as active recipe after upload")
    args = parser.parse_args()

    if not os.path.isfile(args.recipe_file):
        print(f"File not found: {args.recipe_file}")
        return 1

    with open(args.recipe_file, encoding="utf-8") as f:
        recipe = json.load(f)

    recipe_id = recipe.get("recipeId", "")
    if not recipe_id:
        print("No recipeId found in JSON")
        return 1

    dcmd_topic = f"spBv1.0/{GROUP}/DCMD/{args.node}/CH{args.channel}"
    ddata_topic = f"spBv1.0/{GROUP}/DDATA/{args.node}/CH{args.channel}"

    print(f"Recipe:  {recipe_id} v{recipe.get('recipeVersion', '?')}")
    print(f"Steps:   {len(recipe.get('steps', []))}")
    print(f"Broker:  {args.broker}:{args.port}")
    print(f"DCMD:    {dcmd_topic}")

    # Build DCMD payload — json field is an embedded object, not a string
    dcmd_payload = {
        "cmd": "recipe_update",
        "recipe_id": recipe_id,
        "json": recipe,
    }
    payload_str = json.dumps(dcmd_payload, separators=(",", ":"))
    print(f"Payload: {len(payload_str)} bytes")

    # Set up response listener
    responses = []

    def on_message(client, userdata, msg):
        try:
            p = json.loads(msg.payload.decode())
            responses.append(p)
        except Exception:
            pass

    client = mqtt.Client()
    client.on_message = on_message
    print(f"\n[1] Connecting to {args.broker}:{args.port} ...")
    try:
        client.connect(args.broker, args.port, 10)
    except Exception as e:
        print(f"  FAIL: {e}")
        return 1

    client.subscribe(ddata_topic)
    client.loop_start()
    time.sleep(1)
    print("  Connected")

    # Send recipe_update
    print(f"\n[2] DCMD recipe_update {recipe_id} ...")
    result = client.publish(dcmd_topic, payload_str.encode("utf-8"), qos=1)
    result.wait_for_publish()
    time.sleep(3)

    # Check for ack
    ack = None
    for r in responses:
        if r.get("type") == "recipe_update_ack":
            ack = r
            break

    if ack and ack.get("status") == "ok":
        print(f"  OK: {ack.get('recipe_id')} stored ({ack.get('size', '?')} bytes)")
    else:
        print(f"  No ack received. Responses: {[r.get('type') for r in responses]}")
        print("  Recipe may still have been stored — check with 'recipe list' on console")

    # Set active if requested
    if args.set_active:
        print(f"\n[3] DCMD recipe_select {recipe_id} ...")
        responses.clear()
        select_payload = json.dumps({"cmd": "recipe_select", "recipe_id": recipe_id})
        result = client.publish(dcmd_topic, select_payload.encode("utf-8"), qos=1)
        result.wait_for_publish()
        time.sleep(2)
        print(f"  Sent")

    client.loop_stop()
    client.disconnect()
    print("\nDone.")
    return 0


if __name__ == "__main__":
    sys.exit(main())
