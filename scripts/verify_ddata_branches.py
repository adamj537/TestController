#!/usr/bin/env python3
"""verify_ddata_branches.py — End-to-end validation for DDATA v2.15.0.

Runs a recipe on the target TC and verifies the final DDATA result contains:
  - branches[]        (at least one entry)
  - fixture_snapshot  (with fixture_type and instruments[])

Usage:
  python3 verify_ddata_branches.py [--tc-ip IP] [--tc-serial SERIAL]

Defaults to TC-002 (192.168.50.46, G3-MB-Tester-002).
"""
import argparse
import json
import socket
import sys
import time
import threading

import paho.mqtt.client as mqtt

GROUP = "SensitMfg"
CHANNEL = 0

result_msg: dict = {}
done = threading.Event()


def on_connect(client: mqtt.Client, userdata, flags, rc) -> None:
    topic = f"spBv1.0/{GROUP}/DDATA/#"
    client.subscribe(topic)
    print(f"MQTT subscribed: {topic}")


def on_message(client: mqtt.Client, userdata, msg: mqtt.MQTTMessage) -> None:
    try:
        obj = json.loads(msg.payload.decode("utf-8"))
    except (json.JSONDecodeError, UnicodeDecodeError):
        # SparkplugB protobuf messages or other binary payloads — skip
        return
    if obj.get("type") == "result":
        result_msg.update(obj)
        done.set()


def tc_cmd(host: str, port: int, cmd: str, wait: float = 1.0) -> str:
    s = socket.socket()
    s.connect((host, port))
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
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--tc-ip",     default="192.168.50.46", help="TC IP address")
    parser.add_argument("--tc-serial", default="G3-MB-Tester-002", help="TC serial/node ID")
    parser.add_argument("--broker",    default="192.168.50.1",  help="MQTT broker IP")
    parser.add_argument("--timeout",   default=90, type=int,    help="Max seconds to wait for result")
    args = parser.parse_args()

    client = mqtt.Client()
    client.on_connect = on_connect
    client.on_message = on_message
    client.connect(args.broker, 1883, 10)
    client.loop_start()
    time.sleep(1)

    print(f"Triggering sm start on {args.tc_ip}...")
    resp = tc_cmd(args.tc_ip, 4242, "sm start", wait=0.5)
    print(f"  console: {resp.strip()!r}")

    print(f"Waiting for result DDATA (up to {args.timeout}s)...")
    done.wait(timeout=args.timeout)
    client.loop_stop()
    client.disconnect()

    if not result_msg:
        print("FAIL: no result DDATA received within timeout")
        sys.exit(1)

    outcome = result_msg.get("outcome", "unknown")
    print(f"\nRecipe outcome: {outcome.upper()}")

    # ── Check branches[] ────────────────────────────────────────────────────
    branches = result_msg.get("branches")
    if branches is None:
        print("FAIL: 'branches' key missing from result DDATA")
        sys.exit(1)
    if not isinstance(branches, list) or len(branches) == 0:
        print(f"FAIL: 'branches' is empty or not a list: {branches!r}")
        sys.exit(1)

    print(f"\nbranches[] — {len(branches)} branch(es):")
    for b in branches:
        b_name    = b.get("branch", "?")
        b_verdict = b.get("verdict", "?")
        entries   = b.get("entries", [])
        print(f"  [{b_verdict}] {b_name}  ({len(entries)} measurement(s))")
        for e in entries:
            print(f"       {e.get('net_id','?'):25s}  {e.get('measured','')} {e.get('unit','')}"
                  f"  [{e.get('verdict','?')}]")

    # ── Check fixture_snapshot ───────────────────────────────────────────────
    snapshot = result_msg.get("fixture_snapshot")
    if snapshot is None:
        print("\nWARN: 'fixture_snapshot' key missing (cal-profile.json not written yet?)")
    else:
        ft          = snapshot.get("fixture_type", "?")
        fw          = snapshot.get("firmware_version", "?")
        cal_date    = snapshot.get("calibration_date", "?")
        cal_expiry  = snapshot.get("calibration_expiry", "?")
        instruments = snapshot.get("instruments", [])
        print(f"\nfixture_snapshot:")
        print(f"  fixture_type:        {ft}")
        print(f"  firmware_version:    {fw}")
        print(f"  calibration_date:    {cal_date}")
        print(f"  calibration_expiry:  {cal_expiry}")
        print(f"  instruments[]:       {len(instruments)} instrument(s)")
        for inst in instruments:
            print(f"    {inst.get('id','?'):20s}  {inst.get('type','?')}")

    # ── Summary ─────────────────────────────────────────────────────────────
    print("\n── Validation summary ──────────────────────────────────────────")
    checks = [
        ("result DDATA received",       True),
        ("branches[] present",          branches is not None and len(branches) > 0),
        ("fixture_snapshot present",    snapshot is not None),
        ("fixture_type populated",      snapshot is not None and snapshot.get("fixture_type") not in (None, "", "?")),
        ("instruments[] populated",     snapshot is not None and len(snapshot.get("instruments", [])) > 0),
    ]
    all_pass = True
    for label, ok in checks:
        status = "PASS" if ok else "FAIL"
        if not ok:
            all_pass = False
        print(f"  {status}  {label}")

    print()
    if all_pass:
        print("DDATA v2.15.0 validation: PASS")
    else:
        print("DDATA v2.15.0 validation: FAIL")
        sys.exit(1)


if __name__ == "__main__":
    main()
