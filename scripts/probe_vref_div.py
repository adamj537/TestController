#!/usr/bin/env python3
"""Validate VREF_DIV circuit — push minimal probe recipe and run it.

Pushes a 9-step probe recipe ("vref-probe") inline via MQTT DCMD, then starts
the test with recipe_id="vref-probe".  The probe sequence is:

  dut_heartbeat → dut_enter_test
  → mux_snapshot "pre_vref"
  → dut_gpio_set PC5 HIGH   (enable LTC2498 VREF)
  → dut_peripheral_adc_read VOUT_2611  (exp 480–720 mV via LTC2498)
  → mux_read MUX3 ch08  (VREF_DIV on TIE, exp 1500–2560 mV)
  → mux_compare_snapshot "pre_vref" (xcheck, ignore MUX3 ch08)
  → dut_gpio_clear PC5
  → dut_exit_test

Monitors the TC TCP console for up to 60s and prints per-step results.
Exits 0 on all PASS, 1 on any FAIL or error.

Usage:
    python3 scripts/probe_vref_div.py [--host <TC_IP>] [--broker <BROKER_IP>]
"""

import argparse
import json
import re
import socket
import sys
import time

import paho.mqtt.client as mqtt

import os
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from tc_config import HOST, PORT, BROKER, TC_SERIAL

MQTT_PORT  = 1883
GROUP      = "SensitMfg"
RECIPE_ID  = "vref-probe"

PROBE_RECIPE: dict = {
    "recipeId":      RECIPE_ID,
    "recipeVersion": "1.0.0",
    "name":          "VREF_DIV Probe",
    "timeoutMs":     60000,
    "steps": [
        {"id": "hb",          "primitive": "dut_heartbeat",
         "label": "DUT Heartbeat",
         "criticality": "CRITICAL", "onError": "abort", "enabled": True},
        {"id": "enter",       "primitive": "dut_enter_test",
         "label": "DUT Enter Test Mode",
         "criticality": "CRITICAL", "onError": "abort", "enabled": True},
        {"id": "pre_snap",    "primitive": "mux_snapshot",
         "label": "TIE Snapshot (pre-VREF)",
         "criticality": "REQUIRED", "onError": "skip", "enabled": True,
         "params": {"name": "pre_vref"}},
        {"id": "vref_ena",    "primitive": "dut_gpio_set",
         "label": "LTC2498 VREF Enable (PC5 HIGH)",
         "criticality": "REQUIRED", "onError": "skip", "enabled": True,
         "params": {"pin": "PC5", "level": "HIGH"}},
        {"id": "vout_2611",   "primitive": "dut_peripheral_adc_read",
         "label": "VOUT_2611 via LTC2498 (exp 480–720 mV)",
         "criticality": "REQUIRED", "onError": "skip", "enabled": True,
         "params": {"channel": "VOUT_2611",
                    "min_mv": 480, "max_mv": 720,
                    "check_id": "ltc_vout_2611"}},
        {"id": "vref_div",    "primitive": "mux_read",
         "label": "VREF_DIV TIE MUX3 ch08 (exp 1500–2560 mV)",
         "criticality": "REQUIRED", "onError": "skip", "enabled": True,
         "params": {"mux": 3, "ch": 8,
                    "min_mv": 1500, "max_mv": 2560,
                    "check_id": "ltc_vref_div"}},
        {"id": "xcheck",      "primitive": "mux_compare_snapshot",
         "label": "TIE Xcheck post-VREF (ignore MUX3 ch08)",
         "criticality": "REQUIRED", "onError": "skip", "enabled": True,
         "params": {"name": "pre_vref", "noise_mv": 100,
                    "ignore": [{"mux": 3, "ch": 8}],
                    "check_id": "ltc_vref_xcheck"}},
        {"id": "vref_dis",    "primitive": "dut_gpio_clear",
         "label": "LTC2498 VREF Disable (PC5 LOW)",
         "criticality": "OPTIONAL", "onError": "skip", "enabled": True,
         "params": {"pin": "PC5"}},
        {"id": "exit",        "primitive": "dut_exit_test",
         "label": "DUT Exit Test Mode",
         "criticality": "REQUIRED", "onError": "skip", "enabled": True},
    ],
}


class TC:
    """Minimal TC TCP console client, matching check_dut_version.py pattern."""

    def __init__(self, host: str, port: int = PORT) -> None:
        self.host = host
        self.port = port
        self.sock: socket.socket | None = None

    def connect(self) -> None:
        self.sock = socket.socket()
        self.sock.settimeout(10)
        self.sock.connect((self.host, self.port))
        time.sleep(0.3)
        self._drain()

    def _drain(self) -> None:
        self.sock.settimeout(0.3)
        try:
            while self.sock.recv(4096):
                pass
        except (OSError, socket.timeout):
            pass
        self.sock.settimeout(10)

    def cmd(self, command: str, wait: float = 3.0) -> str:
        self.sock.sendall((command + "\n").encode())
        out: list[str] = []
        deadline = time.time() + wait
        self.sock.settimeout(1.0)
        while time.time() < deadline:
            try:
                chunk = self.sock.recv(8192).decode(errors="replace")
                if chunk:
                    out.append(chunk)
                    if "g3-tc|" in chunk and ">" in chunk:
                        break
            except (OSError, socket.timeout):
                pass
        return "".join(out)

    def read_until(self, stop_markers: list[str], timeout: float) -> str:
        """Read from TCP console until any stop_marker appears or timeout."""
        out: list[str] = []
        deadline = time.time() + timeout
        self.sock.settimeout(1.0)
        while time.time() < deadline:
            try:
                chunk = self.sock.recv(8192).decode(errors="replace")
                if chunk:
                    out.append(chunk)
                    joined = "".join(out)
                    if any(m in joined for m in stop_markers):
                        time.sleep(0.5)
                        try:
                            out.append(self.sock.recv(4096).decode(errors="replace"))
                        except (OSError, socket.timeout):
                            pass
                        break
            except (OSError, socket.timeout):
                pass
        return "".join(out)

    def close(self) -> None:
        if self.sock:
            self.sock.close()
            self.sock = None


def mqtt_send(broker: str, topic: str, payload: dict) -> None:
    payload_bytes = json.dumps(payload).encode()
    connected: list[bool] = [False]
    published: list[bool] = [False]

    def on_connect(c, u, f, rc, p=None) -> None:
        connected[0] = rc == 0

    def on_publish(c, u, mid, rc=None, p=None) -> None:
        published[0] = True

    c = mqtt.Client(mqtt.CallbackAPIVersion.VERSION2, client_id="probe-vref-div")
    c.on_connect = on_connect
    c.on_publish = on_publish
    c.connect(broker, MQTT_PORT, keepalive=10)
    c.loop_start()

    t = time.time()
    while not connected[0] and time.time() - t < 5:
        time.sleep(0.1)
    if not connected[0]:
        c.loop_stop()
        raise RuntimeError(f"Cannot connect to broker {broker}:{MQTT_PORT}")

    c.publish(topic, payload_bytes, qos=1)
    t = time.time()
    while not published[0] and time.time() - t < 5:
        time.sleep(0.1)

    c.loop_stop()
    c.disconnect()


def parse_step_results(text: str) -> list[tuple[str, str, str]]:
    """Return [(step_id, outcome, detail)] from console output."""
    results: list[tuple[str, str, str]] = []
    for line in text.splitlines():
        m = re.search(r"\[(PASS|FAIL|INFO|SKIP)\]\s+(\S+):\s*(.*)", line)
        if m:
            results.append((m.group(2), m.group(1), m.group(3).strip()))
    return results


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__,
                                     formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--host",   default=HOST,   help=f"TC IP (default: {HOST})")
    parser.add_argument("--broker", default=BROKER, help=f"Broker IP (default: {BROKER})")
    parser.add_argument("--serial", default=TC_SERIAL,
                        help=f"TC serial (default: {TC_SERIAL})")
    args = parser.parse_args()

    dcmd_topic = f"spBv1.0/{GROUP}/DCMD/{args.serial}/CH0"

    print("=" * 55)
    print("  VREF_DIV Probe")
    print("=" * 55)

    # ── Step 1: Connect TC console ─────────────────────────────────────────
    print(f"\n[1] Connect to TC console {args.host}:{PORT}")
    tc = TC(args.host)
    try:
        tc.connect()
    except Exception as e:
        print(f"    FAILED: {e}")
        return 1
    print("    Connected")

    # ── Step 2: Check TC is idle ───────────────────────────────────────────
    print("\n[2] State check")
    resp = tc.cmd("sm status", wait=3.0)
    for line in resp.splitlines():
        if "state" in line.lower() or "State" in line:
            print(f"    {line.strip()}")

    # ── Step 3: Push probe recipe via recipe_update DCMD ──────────────────
    print(f"\n[3] Push probe recipe '{RECIPE_ID}' via DCMD")
    payload_bytes = json.dumps({"cmd": "recipe_update",
                                "recipe_id": RECIPE_ID,
                                "json": PROBE_RECIPE}).encode()
    print(f"    Payload: {len(payload_bytes)} bytes → {dcmd_topic}")
    try:
        mqtt_send(args.broker, dcmd_topic,
                  {"cmd": "recipe_update", "recipe_id": RECIPE_ID, "json": PROBE_RECIPE})
        print("    Published")
    except Exception as e:
        print(f"    FAILED: {e}")
        tc.close()
        return 1

    time.sleep(1.5)  # let TC process recipe_update

    # ── Step 4: Trigger start with probe recipe ────────────────────────────
    print(f"\n[4] Start test with recipe_id='{RECIPE_ID}'")
    try:
        mqtt_send(args.broker, dcmd_topic,
                  {"cmd": "start", "recipe_id": RECIPE_ID})
        print("    Published")
    except Exception as e:
        print(f"    FAILED: {e}")
        tc.close()
        return 1

    # ── Step 5: Capture console output until test ends ─────────────────────
    print("\n[5] Capturing TC console (up to 60s)...\n")
    stop = ["outcome=pass", "outcome=fail", "outcome=abort",
            "PASS", "FAIL",   # recipe result state
            "State: Pass", "State: Fail", "State: Fault",
            "recipe done", "result DDATA"]
    raw = tc.read_until(stop, timeout=60.0)
    tc.close()

    # ── Step 6: Print key lines ────────────────────────────────────────────
    print("-" * 55)
    key_terms = ("PASS", "FAIL", "INFO", "SKIP", "mux_read", "dut_peripheral",
                 "mux_snapshot", "mux_compare", "dut_gpio", "dut_heartbeat",
                 "dut_enter_test", "dut_exit_test", "outcome", "State")
    for line in raw.splitlines():
        if any(t in line for t in key_terms):
            print(f"  {line.strip()}")
    print("-" * 55)

    # ── Step 7: Report targeted steps ─────────────────────────────────────
    results = parse_step_results(raw)
    vout_result = next((r for r in results if "dut_peripheral_adc_read" in r[0] or
                        "ltc_vout_2611" in r[2] or "VOUT_2611" in r[2]), None)
    vref_result = next((r for r in results if "mux_read" in r[0] or
                        "ltc_vref_div" in r[2] or "mux=3 ch=8" in r[2]), None)

    print()
    print("  Key measurements:")
    if vout_result:
        print(f"    VOUT_2611 (LTC2498) : [{vout_result[1]}]  {vout_result[2]}")
    else:
        print("    VOUT_2611 (LTC2498) : (not found in output)")
    if vref_result:
        print(f"    VREF_DIV  (MUX3 ch8): [{vref_result[1]}]  {vref_result[2]}")
    else:
        print("    VREF_DIV  (MUX3 ch8): (not found in output)")

    any_fail = any(r[1] == "FAIL" for r in results)
    print()
    print("=" * 55)
    print(f"  Result: {'FAIL' if any_fail else 'PASS'}")
    print("=" * 55)
    return 1 if any_fail else 0


if __name__ == "__main__":
    sys.exit(main())
