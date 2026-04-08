#!/usr/bin/env python3
"""tc_sm_run_watch.py — Start SM, watch TCP console output, and monitor MQTT DDATA.

Runs sm start over the TC TCP console and simultaneously subscribes to the
Sparkplug DDATA topic so step results, state transitions, and the final
outcome are captured from both streams.

Usage: python3 scripts/tc_sm_run_watch.py [--tc-ip IP] [--broker IP] [--timeout 120]
"""

import argparse
import json
import socket
import threading
import time
import sys
import os

import paho.mqtt.client as mqtt

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from tc_config import HOST, PORT, BROKER, TC_SERIAL

TC_PORT   = PORT
MQTT_PORT = 1883
GROUP     = "SensitMfg"
CHANNEL   = 0

# Signals between threads
_done      = threading.Event()   # set when recipe completes or timeout
_sm_started = threading.Event()  # set after sm start is sent — gate result processing
_outcome: list[str | None] = [None]


# ── MQTT ─────────────────────────────────────────────────────────────────────

def _on_connect(client: mqtt.Client, userdata: object, flags: object, reason_code: object, properties: object) -> None:
    topic = f"spBv1.0/{GROUP}/DDATA/{TC_SERIAL}/CH{CHANNEL}"
    client.subscribe(topic)
    print(f"[MQTT] subscribed → {topic}", flush=True)


def _on_message(client: mqtt.Client, userdata: object, msg: mqtt.MQTTMessage) -> None:
    try:
        payload = json.loads(msg.payload.decode("utf-8", errors="replace"))
    except json.JSONDecodeError:
        print(f"[MQTT] raw: {msg.payload[:200]}", flush=True)
        return

    # State transition — also treat terminal states as done
    if "state" in payload:
        state = payload["state"]
        print(f"[MQTT] state → {state}", flush=True)
        if state in ("Pass", "Fail", "Aborted") and _sm_started.is_set():
            _outcome[0] = state.lower()
            _done.set()

    # Pre-gate result
    if "pre_gate_result" in payload:
        pg = payload["pre_gate_result"]
        print(f"[MQTT] pre_gate: outcome={pg.get('outcome')}  pfw_loaded={pg.get('pfw_loaded')}", flush=True)

    # Step result (individual step pass/fail/skip)
    if "step_result" in payload:
        sr = payload["step_result"]
        status = sr.get("status", "?")
        step   = sr.get("step_name", sr.get("check_id", "?"))
        detail = sr.get("detail", "")
        print(f"[MQTT] step  [{status:4s}] {step}  {detail}".rstrip(), flush=True)

    # Selftest / DDATA array (fixture snapshot)
    if "checks" in payload:
        checks = payload["checks"]
        n_pass = sum(1 for c in checks if c.get("status") == "pass")
        n_fail = sum(1 for c in checks if c.get("status") == "fail")
        print(f"[MQTT] selftest: {n_pass} pass  {n_fail} fail  ({len(checks)} checks)", flush=True)

    # Final result — only process after sm start to ignore retained stale messages
    if "outcome" in payload and _sm_started.is_set():
        outcome  = payload["outcome"]
        dur_ms   = payload.get("duration_ms", payload.get("dur_ms", "?"))
        branches = payload.get("branches", [])
        n_pass   = sum(1 for b in branches if b.get("verdict") == "PASS")
        n_fail   = sum(1 for b in branches if b.get("verdict") == "FAIL")
        print(f"[MQTT] ── RESULT: outcome={outcome}  branches={len(branches)} ({n_pass} PASS / {n_fail} FAIL)  duration={dur_ms}ms ──", flush=True)
        # Print per-branch summary
        for b in branches:
            nets = b.get("nets", [])
            print(f"[MQTT]   {b.get('branch','?'):30s}  {b.get('verdict','?'):4s}  ({len(nets)} nets)", flush=True)
        _outcome[0] = outcome
        _done.set()


def _mqtt_thread(broker: str) -> None:
    client = mqtt.Client(mqtt.CallbackAPIVersion.VERSION2)
    client.on_connect = _on_connect
    client.on_message = _on_message
    try:
        client.connect(broker, MQTT_PORT, keepalive=60)
        client.loop_start()
        _done.wait()
        client.loop_stop()
        client.disconnect()
    except OSError as e:
        print(f"[MQTT] connect failed: {e}", flush=True)


# ── TCP console ───────────────────────────────────────────────────────────────

def _tcp_connect(ip: str) -> socket.socket:
    s = socket.socket()
    s.settimeout(10)
    s.connect((ip, TC_PORT))
    time.sleep(0.3)
    s.settimeout(0.5)
    try:
        while True:
            s.recv(4096)
    except socket.timeout:
        pass
    s.settimeout(10)
    return s


def _tcp_cmd(s: socket.socket, c: str, wait: float = 3.0) -> str:
    s.sendall((c + "\n").encode())
    out: list[str] = []
    deadline = time.time() + wait
    s.settimeout(1)
    while time.time() < deadline:
        try:
            chunk = s.recv(4096).decode("utf-8", errors="replace")
            if chunk:
                out.append(chunk)
                if "g3-tc|" in chunk and ">" in chunk:
                    break
        except socket.timeout:
            continue
    return "".join(out)


def _tcp_watch(s: socket.socket, wait: float) -> str:
    out: list[str] = []
    deadline = time.time() + wait
    s.settimeout(1)
    while time.time() < deadline:
        if _done.is_set():
            time.sleep(1.0)  # collect any trailing console output
            break
        try:
            chunk = s.recv(4096).decode("utf-8", errors="replace")
            if chunk:
                out.append(chunk)
                print(chunk, end="", flush=True)
                if ("state: Idle" in chunk or "state=Idle" in chunk or
                        "recipe_done:" in chunk or
                        "Testing → Pass" in chunk or "Testing → Fail" in chunk or
                        "pre_gate_fail" in chunk or
                        "current_low" in chunk or "vdut_uncalibrated" in chunk):
                    time.sleep(1.0)
                    _done.set()
        except socket.timeout:
            continue
    return "".join(out)


# ── Main ──────────────────────────────────────────────────────────────────────

def main() -> None:
    p = argparse.ArgumentParser()
    p.add_argument("--tc-ip",  default=HOST)
    p.add_argument("--broker", default=BROKER)
    p.add_argument("--timeout", type=float, default=120.0)
    args = p.parse_args()

    # Start MQTT subscriber thread first so we don't miss early messages
    t = threading.Thread(target=_mqtt_thread, args=(args.broker,), daemon=True)
    t.start()
    time.sleep(0.5)  # give MQTT time to connect before sm start

    s = _tcp_connect(args.tc_ip)
    _tcp_cmd(s, "sm abort", 2.0)
    _tcp_cmd(s, "vdac off", 2.0)
    time.sleep(0.5)

    print("=" * 60)
    print("  sm start — watching TCP console + MQTT DDATA")
    print("=" * 60)
    _tcp_cmd(s, "sm start", 1.0)
    _sm_started.set()  # gate: now process MQTT result messages
    _tcp_watch(s, args.timeout)

    # If TCP watch timed out without MQTT completing, set done
    _done.set()
    t.join(timeout=3.0)
    s.close()

    print("\n" + "=" * 60)
    outcome = _outcome[0]
    if outcome:
        print(f"  Final outcome: {outcome.upper()}")
    else:
        print("  No MQTT result received (timeout or broker unreachable)")
    print("=" * 60)


if __name__ == "__main__":
    main()
