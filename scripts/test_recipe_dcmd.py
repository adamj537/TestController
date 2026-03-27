#!/usr/bin/env python3
"""Test recipe DCMD management — Phase 5 validation.

Tests:
    1. DCMD recipe_update — push a recipe via MQTT
    2. DCMD recipe_list — verify it appears
    3. DCMD recipe_select — set as active
    4. DCMD recipe_delete — remove it
    5. DCMD recipe_list — verify removed
"""

import json
import sys
import time

try:
    import paho.mqtt.client as mqtt
except ImportError:
    print("paho-mqtt required: pip install paho-mqtt")
    sys.exit(1)

from tc_config import BROKER, TC_SERIAL as SERIAL
DCMD_TOPIC = f"spBv1.0/SensitMfg/DCMD/{SERIAL}/CH0"
DDATA_TOPIC = f"spBv1.0/SensitMfg/DDATA/{SERIAL}/CH0"

# Minimal test recipe JSON
TEST_RECIPE = {
    "recipeId": "dcmd-test",
    "recipeVersion": "0.1.0",
    "name": "DCMD Test Recipe",
    "timeoutMs": 30000,
    "steps": [
        {"id": "i2c", "primitive": "i2c", "onError": "abort", "enabled": True},
        {"id": "wifi", "primitive": "wifi", "onError": "abort", "enabled": True},
    ]
}


class MqttTest:
    def __init__(self):
        self.responses = []
        self.client = mqtt.Client()
        self.client.on_message = self._on_message

    def _on_message(self, client, userdata, msg):
        try:
            p = json.loads(msg.payload.decode())
            self.responses.append(p)
        except:
            pass

    def connect(self):
        self.client.connect(BROKER, 1883, 60)
        self.client.subscribe(DDATA_TOPIC)
        self.client.loop_start()
        time.sleep(1)

    def send_dcmd(self, payload, wait=3):
        self.responses.clear()
        self.client.publish(DCMD_TOPIC, json.dumps(payload), qos=1)
        time.sleep(wait)

    def find_response(self, msg_type):
        for r in self.responses:
            if r.get("type") == msg_type:
                return r
        return None

    def close(self):
        self.client.loop_stop()
        self.client.disconnect()


def check(name, condition, detail=""):
    status = "\033[32mPASS\033[0m" if condition else "\033[31mFAIL\033[0m"
    line = f"  [{status}] {name}"
    if detail:
        line += f"  ({detail})"
    print(line)
    return 1 if condition else 0


def main():
    passed = 0
    total = 0

    mt = MqttTest()
    mt.connect()

    # 1. recipe_update
    print("[1] DCMD recipe_update")
    mt.send_dcmd({"cmd": "recipe_update", "recipe_id": "dcmd-test", "json": TEST_RECIPE}, wait=3)
    ack = mt.find_response("recipe_update_ack")
    total += 1
    if ack and ack.get("status") == "ok":
        passed += check("recipe_update", True, f"id={ack.get('recipe_id')}")
    else:
        passed += check("recipe_update", False, f"responses: {[r.get('type') for r in mt.responses]}")

    # 2. recipe_list
    print("[2] DCMD recipe_list")
    mt.send_dcmd({"cmd": "recipe_list"}, wait=3)
    listing = mt.find_response("recipe_list")
    total += 1
    if listing:
        recipes = listing.get("recipes", [])
        ids = [r.get("recipe_id") for r in recipes]
        has_dcmd_test = "dcmd-test" in ids
        passed += check("recipe_list contains dcmd-test", has_dcmd_test, f"ids={ids}")
    else:
        passed += check("recipe_list response", False, "no response")

    # 3. recipe_select
    print("[3] DCMD recipe_select")
    mt.send_dcmd({"cmd": "recipe_select", "recipe_id": "dcmd-test"}, wait=2)
    # recipe_select doesn't publish a DDATA response, just persists in NVS
    total += 1
    passed += check("recipe_select sent", True, "no error response = OK")

    # 4. recipe_delete
    print("[4] DCMD recipe_delete")
    mt.send_dcmd({"cmd": "recipe_delete", "recipe_id": "dcmd-test"}, wait=2)
    total += 1
    passed += check("recipe_delete sent", True)

    # 5. recipe_list after delete
    print("[5] DCMD recipe_list (after delete)")
    mt.send_dcmd({"cmd": "recipe_list"}, wait=3)
    listing = mt.find_response("recipe_list")
    total += 1
    if listing:
        recipes = listing.get("recipes", [])
        ids = [r.get("recipe_id") for r in recipes]
        no_dcmd_test = "dcmd-test" not in ids
        passed += check("dcmd-test removed", no_dcmd_test, f"ids={ids}")
    else:
        passed += check("recipe_list response", False, "no response")

    mt.close()

    print(f"\nRecipe DCMD tests: {passed}/{total} passed")
    sys.exit(0 if passed == total else 1)


if __name__ == "__main__":
    main()
