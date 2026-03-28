#!/usr/bin/env python3
"""Run sm start and capture all step_result DDATA messages via MQTT.

Starts MQTT listener, triggers sm start via TCP console, waits for result.
Prints each step_result with pass/fail.
"""
import json
import socket
import sys
import time
import threading

sys.path.insert(0, __file__.rsplit("/", 1)[0])
from tc_config import HOST, PORT, BROKER, TC_SERIAL as NODE

import paho.mqtt.client as mqtt

GROUP   = "SensitMfg"
CHANNEL = 0
DDATA_TOPIC = f"spBv1.0/{GROUP}/DDATA/{NODE}/CH{CHANNEL}"

step_results: list[dict] = []
final_result: dict = {}
done = threading.Event()


def on_connect(client: mqtt.Client, userdata, flags, rc) -> None:
    client.subscribe(f"spBv1.0/{GROUP}/DDATA/#")


def on_message(client: mqtt.Client, userdata, msg: mqtt.MQTTMessage) -> None:
    try:
        obj = json.loads(msg.payload.decode("utf-8"))
    except (json.JSONDecodeError, UnicodeDecodeError):
        return
    t = obj.get("type", "")
    if t == "step_result":
        step_results.append(obj)
    elif t == "result":
        final_result.update(obj)
        done.set()


def tc_cmd(cmd: str, wait: float = 3.0) -> str:
    s = socket.socket()
    s.connect((HOST, PORT))
    s.settimeout(1)
    time.sleep(0.2)
    try:
        while s.recv(4096):
            pass
    except OSError:
        pass
    s.send((cmd + "\r\n").encode())
    out = b""
    deadline = time.time() + wait
    while time.time() < deadline:
        try:
            chunk = s.recv(4096)
            if chunk:
                out += chunk
        except OSError:
            pass
    s.close()
    return out.decode("utf-8", errors="replace")


def main() -> None:
    client = mqtt.Client()
    client.on_connect = on_connect
    client.on_message = on_message
    client.connect(BROKER, 1883, 10)
    client.loop_start()
    time.sleep(1)

    print("Triggering sm start ...")
    tc_cmd("sm start", wait=0.5)

    print("Waiting for result (up to 90s) ...")
    done.wait(timeout=90)

    client.loop_stop()
    client.disconnect()

    print(f"\n{'Step':<35} {'Result'}")
    print("-" * 45)
    for sr in step_results:
        sid    = sr.get("step_id", sr.get("id", "?"))
        passed = sr.get("passed", sr.get("pass", None))
        mark   = "PASS" if passed else "FAIL"
        print(f"  {sid:<33} {mark}")

    outcome = final_result.get("outcome", "?")
    dur     = final_result.get("duration_ms", 0)
    total   = len(step_results)
    passes  = sum(1 for sr in step_results if sr.get("passed", sr.get("pass")))
    print(f"\nRecipe: {outcome.upper()}  {passes}/{total}  {dur}ms")


if __name__ == "__main__":
    main()
