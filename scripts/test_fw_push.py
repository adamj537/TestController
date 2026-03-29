#!/usr/bin/env python3
"""Test fw_push DCMD — send firmware to TC, verify fw_push_status DDATA response.

Usage:
    python3 scripts/test_fw_push.py --fw-type dut_fw --url http://192.168.50.19:8080/pfw.bin --version 0.1.1
    python3 scripts/test_fw_push.py --fw-type prod_fw --url http://192.168.50.19:8080/product.bin --version 1.0.46
    python3 scripts/test_fw_push.py --fw-type dut_fw --url http://192.168.50.19:8080/pfw.bin --version 0.1.1 --sha256 abc123...

After fw_push completes, also sends versions_get to verify the stored version.
"""
import argparse
import hashlib
import json
import sys
import time

import paho.mqtt.client as mqtt

from tc_config import BROKER, TC_SERIAL as NODE
PORT = 1883
GROUP = "SensitMfg"
CHANNEL = 0

DCMD_TOPIC = f"spBv1.0/{GROUP}/DCMD/{NODE}/CH{CHANNEL}"
DDATA_TOPIC = f"spBv1.0/{GROUP}/DDATA/{NODE}/CH{CHANNEL}"


def main() -> int:
    p = argparse.ArgumentParser(description="Test fw_push DCMD")
    p.add_argument("--fw-type", required=True, choices=["dut_fw", "prod_fw"])
    p.add_argument("--url", required=True, help="HTTP URL to firmware binary")
    p.add_argument("--version", default="", help="Version string (e.g. 0.1.1)")
    p.add_argument("--sha256", default="", help="Expected SHA-256 hex (omit to skip verify)")
    p.add_argument("--broker", default=BROKER)
    p.add_argument("--node", default=NODE)
    p.add_argument("--timeout", type=int, default=30, help="Seconds to wait for response")
    args = p.parse_args()

    dcmd_topic = f"spBv1.0/{GROUP}/DCMD/{args.node}/CH{CHANNEL}"
    ddata_topic = f"spBv1.0/{GROUP}/DDATA/{args.node}/CH{CHANNEL}"

    received: list[dict] = []
    connected = False

    def on_connect(client: mqtt.Client, userdata: None, flags: dict, rc: int) -> None:
        nonlocal connected
        connected = True
        client.subscribe(f"spBv1.0/{GROUP}/#")

    def on_message(client: mqtt.Client, userdata: None, msg: mqtt.MQTTMessage) -> None:
        try:
            obj = json.loads(msg.payload.decode())
            received.append(obj)
            msg_type = obj.get("type", "?")
            if msg_type in ("fw_push_status", "versions"):
                print(f"\n[RX] type={msg_type}")
                print(json.dumps(obj, indent=2))
        except json.JSONDecodeError:
            pass

    client = mqtt.Client()
    client.on_connect = on_connect
    client.on_message = on_message
    client.connect(args.broker, PORT, 10)
    client.loop_start()

    # Wait for connection
    deadline = time.time() + 5
    while not connected and time.time() < deadline:
        time.sleep(0.1)
    if not connected:
        print("FAIL: could not connect to MQTT broker")
        return 1
    print(f"Connected to {args.broker}")
    time.sleep(1)

    # Build and send fw_push DCMD
    dcmd: dict[str, str] = {
        "cmd": "fw_push",
        "fw_type": args.fw_type,
        "url": args.url,
    }
    if args.version:
        dcmd["version"] = args.version
    if args.sha256:
        dcmd["sha256"] = args.sha256

    payload = json.dumps(dcmd)
    print(f"\n[TX] fw_push DCMD -> {dcmd_topic}")
    print(f"  fw_type={args.fw_type}  version={args.version or '(none)'}  url={args.url}")
    client.publish(dcmd_topic, payload)

    # Wait for fw_push_status response
    print(f"\nWaiting up to {args.timeout}s for fw_push_status DDATA ...")
    deadline = time.time() + args.timeout
    fw_push_result = None
    while time.time() < deadline:
        time.sleep(0.5)
        for msg in received:
            if msg.get("type") == "fw_push_status":
                fw_push_result = msg
                break
        if fw_push_result:
            break

    if not fw_push_result:
        print("\nFAIL: no fw_push_status DDATA received within timeout")
        client.loop_stop()
        client.disconnect()
        return 1

    # Evaluate result
    status = fw_push_result.get("status", "?")
    result_fw_type = fw_push_result.get("fw_type", "?")
    result_version = fw_push_result.get("version", "")
    result_size = fw_push_result.get("size", 0)
    result_error = fw_push_result.get("error_msg", "")

    print(f"\n--- fw_push result ---")
    print(f"  status:    {status}")
    print(f"  fw_type:   {result_fw_type}")
    print(f"  version:   {result_version}")
    print(f"  size:      {result_size}")
    if result_error:
        print(f"  error_msg: {result_error}")

    if status != "ok":
        print(f"\nFAIL: fw_push returned status={status}")
        client.loop_stop()
        client.disconnect()
        return 1

    # Now send versions_get to verify the version appears
    print(f"\n[TX] versions_get DCMD ...")
    received.clear()
    client.publish(dcmd_topic, json.dumps({"cmd": "versions_get"}))

    deadline = time.time() + 10
    versions_result = None
    while time.time() < deadline:
        time.sleep(0.5)
        for msg in received:
            if msg.get("type") == "versions":
                versions_result = msg
                break
        if versions_result:
            break

    if not versions_result:
        print("WARN: no versions DDATA received after versions_get")
    else:
        pfw_ver = versions_result.get("Versions/pfw", "")
        prod_ver = versions_result.get("Versions/product_fw", "")
        print(f"\n--- versions ---")
        print(f"  tc:         {versions_result.get('Versions/tc', '?')}")
        print(f"  pfw:        {pfw_ver or '(not set)'}")
        print(f"  product_fw: {prod_ver or '(not set)'}")

        # Verify expected version appears
        if args.fw_type == "dut_fw" and args.version:
            if pfw_ver == args.version:
                print(f"\n  [PASS] PFW version matches: {pfw_ver}")
            else:
                print(f"\n  [FAIL] PFW version mismatch: expected={args.version} got={pfw_ver}")
        elif args.fw_type == "prod_fw" and args.version:
            if prod_ver == args.version:
                print(f"\n  [PASS] Product FW version matches: {prod_ver}")
            else:
                print(f"\n  [FAIL] Product FW version mismatch: expected={args.version} got={prod_ver}")

    client.loop_stop()
    client.disconnect()
    print("\nDone.")
    return 0


if __name__ == "__main__":
    sys.exit(main())
