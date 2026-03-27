#!/usr/bin/env python3
"""check_dut_version.py — Read live DUT PFW version and compare to TC's stored version.

Sequence:
  1. Send versions_get DCMD via MQTT → capture TC's stored Versions/pfw
  2. Connect to TC console via TCP
  3. Pause DUT auto-start (suppress precheck loop)
  4. Enable VDUT at 3300 mV + assert PB-A
  5. Wait for DUT firmware to boot
  6. Run selftest all → parse FW_VERSION from DUT UART response
  7. Teardown: mux release, vdac off, resume auto-start
  8. Compare live DUT version vs TC stored version and report

Usage:
    python3 scripts/check_dut_version.py [--host 10.0.0.244] [--broker 10.0.0.178]

Exit codes:
    0 = versions match (or both unknown)
    1 = mismatch, error, or DUT not present
"""

import argparse
import json
import re
import socket
import sys
import time
import threading

import paho.mqtt.client as mqtt

HOST     = "10.0.0.244"
PORT     = 4242
BROKER   = "10.0.0.178"
MQTT_PORT = 1883
GROUP    = "SensitMfg"
NODE     = "G3-MB-Tester-000"
CHANNEL  = 0
BOOT_WAIT = 2.5  # seconds for DUT firmware to boot and be ready for UART


# ── TC class (shared with other test scripts) ────────────────────────────────

class TC:
    def __init__(self, host: str = HOST):
        self.host = host
        self.sock: socket.socket | None = None

    def connect(self) -> None:
        self.sock = socket.socket()
        self.sock.settimeout(10)
        self.sock.connect((self.host, PORT))
        time.sleep(0.3)
        self._drain()

    def _drain(self) -> None:
        self.sock.settimeout(0.3)
        try:
            while self.sock.recv(4096):
                pass
        except socket.timeout:
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
            except socket.timeout:
                continue
        return "".join(out)

    def cmd_long(self, command: str, stop_marker: str = "Results:", timeout: float = 30.0) -> str:
        self.sock.sendall((command + "\n").encode())
        out: list[str] = []
        deadline = time.time() + timeout
        self.sock.settimeout(1.0)
        while time.time() < deadline:
            try:
                chunk = self.sock.recv(8192).decode(errors="replace")
                if chunk:
                    out.append(chunk)
                    if stop_marker in chunk:
                        time.sleep(0.3)
                        try:
                            out.append(self.sock.recv(4096).decode(errors="replace"))
                        except socket.timeout:
                            pass
                        break
            except socket.timeout:
                continue
        return "".join(out)

    def close(self) -> None:
        if self.sock:
            self.sock.close()
            self.sock = None


# ── MQTT: fetch TC stored version ────────────────────────────────────────────

def get_tc_stored_pfw_version(broker: str, node: str, channel: int) -> str | None:
    """Send versions_get DCMD and wait for the DDATA versions response.
    Returns the Versions/pfw string, or None if not received within timeout."""

    dcmd_topic = f"spBv1.0/{GROUP}/DCMD/{node}/CH{channel}"
    ddata_topic = f"spBv1.0/{GROUP}/DDATA/{node}/CH{channel}"

    result: list[str | None] = [None]
    event = threading.Event()

    def on_connect(client, userdata, flags, rc):
        client.subscribe(ddata_topic)

    def on_message(client, userdata, msg):
        try:
            obj = json.loads(msg.payload.decode("utf-8", errors="replace"))
            if obj.get("type") == "versions" and "Versions/pfw" in obj:
                result[0] = obj["Versions/pfw"]
                event.set()
        except (json.JSONDecodeError, KeyError):
            pass

    client = mqtt.Client()
    client.on_connect = on_connect
    client.on_message = on_message
    client.connect(broker, MQTT_PORT, 5)
    client.loop_start()

    # Give the subscription a moment to establish, then request
    time.sleep(0.5)
    payload = json.dumps({"cmd": "versions_get"})
    client.publish(dcmd_topic, payload.encode("utf-8"))

    event.wait(timeout=8.0)
    client.loop_stop()
    client.disconnect()

    return result[0]


# ── Parse FW_VERSION from selftest all output ─────────────────────────────────

def parse_fw_version(text: str) -> str | None:
    """Extract FW_VERSION:x.y.z+N from selftest dut_version output."""
    m = re.search(r"FW_VERSION:(\S+)", text)
    return m.group(1) if m else None


# ── Teardown ─────────────────────────────────────────────────────────────────

def teardown(tc: TC) -> None:
    tc.cmd("mux release", wait=2)
    tc.cmd("vdac off", wait=2)
    tc.cmd("dut resume", wait=2)
    print("  [teardown] PB-A released, VDUT off, auto-start resumed")


# ── Main ─────────────────────────────────────────────────────────────────────

def main() -> None:
    parser = argparse.ArgumentParser(
        description="Compare live DUT PFW version to TC stored version",
        formatter_class=argparse.RawDescriptionHelpFormatter,
        epilog=__doc__,
    )
    parser.add_argument("--host",   default=HOST,   help=f"TC IP (default: {HOST})")
    parser.add_argument("--broker", default=BROKER, help=f"MQTT broker IP (default: {BROKER})")
    parser.add_argument("--node",   default=NODE,   help=f"TC node ID (default: {NODE})")
    parser.add_argument("--channel", type=int, default=CHANNEL,
                        help=f"TC channel (default: {CHANNEL})")
    parser.add_argument("--boot-wait", type=float, default=BOOT_WAIT,
                        help=f"DUT boot wait seconds (default: {BOOT_WAIT})")
    args = parser.parse_args()

    ok = True

    # ── Step 1: TC stored PFW version (MQTT) ─────────────────────────────────
    print(f"[1] Fetching TC stored PFW version from MQTT ({args.broker})...")
    tc_stored = get_tc_stored_pfw_version(args.broker, args.node, args.channel)
    if tc_stored:
        print(f"    TC stored Versions/pfw = {tc_stored}")
    else:
        print("    TC stored Versions/pfw = (not available — no stored firmware?)")

    # ── Step 2: Connect TCP console ───────────────────────────────────────────
    print(f"\n[2] Connect to TC console {args.host}:{PORT}")
    tc = TC(args.host)
    try:
        tc.connect()
        print("    Connected")
    except Exception as e:
        print(f"    FAILED: {e}")
        sys.exit(1)

    # ── Step 3: Pause DUT auto-start ──────────────────────────────────────────
    print("\n[3] Pause DUT auto-start")
    tc.cmd("dut pause", wait=2)

    # ── Step 4: DUT present? ──────────────────────────────────────────────────
    print("\n[4] DUT presence check")
    resp = tc.cmd("dut sample", wait=5)
    adc_mv = -1
    for line in resp.splitlines():
        if "DUT sample:" in line and "mV" in line:
            try:
                adc_mv = int(line.split(":")[1].strip().split()[0])
            except (IndexError, ValueError):
                pass
            break

    present = 0 < adc_mv < 2000
    print(f"    ADC = {adc_mv} mV  → {'PRESENT' if present else 'ABSENT'}")
    if not present:
        print("    ERROR: DUT not seated — cannot read live version")
        tc.cmd("dut resume", wait=2)
        tc.close()
        sys.exit(1)

    # ── Step 5: Power DUT ─────────────────────────────────────────────────────
    print("\n[5] Enable VDUT 3300 mV + assert PB-A")
    tc.cmd("vdac_voltage both 3300", wait=3)
    tc.cmd("mux select 0 0", wait=2)
    print(f"    Waiting {args.boot_wait:.1f}s for DUT to boot...")
    time.sleep(args.boot_wait)

    # ── Step 6: Run selftest dut_version → parse DUT version ─────────────────
    # Uses targeted subcommand: enter_test → VERSION → exit_test.
    # Assumes DUT is already powered (VDUT on, PB-A asserted above).
    print("\n[6] Running selftest dut_version (enter_test → VERSION → exit_test)...")
    resp = tc.cmd_long("selftest dut_version", stop_marker="Results:", timeout=10.0)
    dut_live = parse_fw_version(resp)

    # Show all DUT UART related lines for diagnostics
    for line in resp.splitlines():
        if any(k in line for k in ("DUT:", "dut_enter_test", "dut_version",
                                    "FW_VERSION", "ENTER_TEST", "VERSION", "[SKIP]",
                                    "[PASS]", "[FAIL]")):
            print(f"    {line.strip()}")

    if dut_live:
        print(f"\n    DUT live FW_VERSION = {dut_live}")
    else:
        print("\n    DUT live FW_VERSION = (not found in selftest output)")
        ok = False

    # ── Teardown ──────────────────────────────────────────────────────────────
    print("\n[7] Teardown")
    teardown(tc)
    tc.close()

    # ── Step 8: Compare ───────────────────────────────────────────────────────
    print("\n" + "=" * 50)
    print(f"  TC stored Versions/pfw : {tc_stored or '(unknown)'}")
    print(f"  DUT live  FW_VERSION   : {dut_live  or '(unknown)'}")
    print()

    if tc_stored and dut_live:
        # Normalize: strip build metadata (+N) for comparison
        tc_base  = tc_stored.split("+")[0]
        dut_base = dut_live.split("+")[0]
        if tc_base == dut_base:
            print(f"  \033[32mMATCH\033[0m  ({tc_base})")
        else:
            print(f"  \033[31mMISMATCH\033[0m  TC={tc_stored}  DUT={dut_live}")
            ok = False
    elif not tc_stored and not dut_live:
        print("  Both versions unknown — cannot compare")
        ok = False
    else:
        print("  One version unknown — partial result")

    sys.exit(0 if ok else 1)


if __name__ == "__main__":
    main()
